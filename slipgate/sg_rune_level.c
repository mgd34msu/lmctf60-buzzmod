#include "../g_local.h"
#undef world
#include "sg_rune_level.h"
#include "sg_rune_game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sg_bot_cvars.h"
#include "sg_rune_bsp.h"
#include "sg_rune_law.h"

#define POST_SLOTS 2
#define POST_FLAGS 2
typedef struct post_set_s
{
	uint32_t flag_cell;
	uint32_t post[POST_SLOTS];
	float facing[POST_SLOTS][3];
	int valid;
} post_set_t;

static post_set_t sg_posts[POST_FLAGS];

sg_rune_level_t sg_rune_level;

static char *sg_level_entities;

void SG_RuneLevelEntities(const char *text)
{
	free(sg_level_entities);
	sg_level_entities = NULL;
	if (text)
	{
		size_t length = strlen(text);

		sg_level_entities = malloc(length + 1U);
		if (sg_level_entities)
			memcpy(sg_level_entities, text, length + 1U);
	}
}

const char *SG_RuneLevelEntityText(void)
{
	return sg_level_entities;
}

/* The live identity: the map file as it is on disk, the entity text the
 * level was spawned from, and the law the engine runs under now.  The
 * artifact must have been generated from the same. */
static int LiveIdentity(const char *game_directory, const char *mapname,
	sg_rune_identity_t *identity, sg_rune_law_t *law)
{
	char path[MAX_OSPATH];
	sg_rune_bsp_t bsp;
	sg_rune_bsp_fault_t fault;

	if (snprintf(path, sizeof(path), "%s/maps/%s.bsp", game_directory, mapname) >=
		(int)sizeof(path) || !SG_RuneBspLoadFile(path, &bsp, &fault))
		return 0;
	if (sg_level_entities && !SG_RuneBspReplaceEntities(&bsp, sg_level_entities))
	{
		SG_RuneBspFree(&bsp);
		return 0;
	}
	SG_RuneLawEngine(law, sv_gravity ? sv_gravity->value : 800.0f);
	memset(identity, 0, sizeof(*identity));
	identity->bsp_crc32 = bsp.file_crc32;
	identity->entity_crc32 = bsp.entity_crc32;
	identity->bsp_bytes = bsp.file_bytes;
	identity->law_crc32 = SG_RuneLawCrc(law);
	identity->schema_id = SG_RUNE_ARTIFACT_SCHEMA_ID;
	SG_RuneBspFree(&bsp);
	return 1;
}

void SG_RuneLevelClear(void)
{
	uint32_t index;

	for (index = 0U; index < SG_RUNE_LEVEL_FIELDS; index++)
		SG_RuneFieldFree(&sg_rune_level.fields[index].field);
	SG_RuneRouterFree(&sg_rune_level.router);
	SG_RuneLocatorFree(&sg_rune_level.locator);
	SG_RuneArtifactRelease(&sg_rune_level.artifact);
	free(sg_rune_level.mechanism_edict);
	memset(&sg_rune_level, 0, sizeof(sg_rune_level));
	memset(sg_posts, 0, sizeof(sg_posts));
	SG_RuneLevelExposureClear();
	for (index = 0U; index < SG_RUNE_LEVEL_FIELDS; index++)
		sg_rune_level.fields[index].destination_cell = SG_RUNE_CX_INDEX_NONE;
}

int SG_RuneLevelCurrent(void)
{
	return sg_rune_level.current;
}

int SG_RuneLevelBegin(const char *mapname)
{
	cvar_t *game_directory_cvar;
	const char *game_directory;
	char path[MAX_OSPATH];
	sg_rune_artifact_status_t status;
	sg_rune_fault_t fault;
	sg_rune_identity_t identity;
	sg_rune_law_t law;
	int os_error = 0;

	SG_RuneLevelClear();
	if (!mapname || !mapname[0])
		return 0;
	game_directory_cvar = gi.cvar("gamedir", "", 0);
	game_directory = game_directory_cvar && game_directory_cvar->string &&
		game_directory_cvar->string[0] ? game_directory_cvar->string : ".";
	if (!SG_RuneArtifactPath(path, sizeof(path), game_directory, mapname))
	{
		gi.dprintf("slipgate: rune refused map=%s: path\n", mapname);
		return 0;
	}
	status = SG_RuneArtifactLoadFile(path, &sg_rune_level.artifact, &os_error,
		&fault);
	if (status != SG_RUNE_ARTIFACT_OK)
	{
		if (status == SG_RUNE_ARTIFACT_FILE_ERROR)
		{
			if (!SG_RuneGameGenerateBusy())
				gi.dprintf("slipgate: no rune for %s (%s): bots hold\n", mapname,
					path);
		}
		else if (fault.array)
			gi.dprintf("slipgate: rune refused map=%s: %s at %s[%u] %s\n",
				mapname, SG_RuneArtifactStatusString(status), fault.array,
				(unsigned int)fault.record, fault.reason);
		else
			gi.dprintf("slipgate: rune refused map=%s: %s\n", mapname,
				SG_RuneArtifactStatusString(status));
		SG_RuneLevelClear();
		return 0;
	}
	if (!LiveIdentity(game_directory, mapname, &identity, &law))
	{
		gi.dprintf("slipgate: rune refused map=%s: the map file could not be "
			"read for its identity\n", mapname);
		SG_RuneLevelClear();
		return 0;
	}
	if (!SG_RuneIdentityMatches(&sg_rune_level.artifact.identity, &identity))
	{
		gi.dprintf("slipgate: rune refused map=%s: identity differs from the "
			"live host (map bytes or laws changed); regenerate with sv rune\n",
			mapname);
		SG_RuneLevelClear();
		return 0;
	}
	if (!SG_RuneLawMatches(&sg_rune_level.artifact.law, &law))
	{
		gi.dprintf("slipgate: rune refused map=%s: law differs from the live "
			"host (gravity, hulls, or frame); regenerate with sv rune\n",
			mapname);
		SG_RuneLevelClear();
		return 0;
	}
	if (!SG_RuneLocatorBuild(&sg_rune_level.locator, &sg_rune_level.artifact) ||
		!SG_RuneRouterBuild(&sg_rune_level.router, &sg_rune_level.artifact))
	{
		gi.dprintf("slipgate: rune refused map=%s: out of memory indexing\n",
			mapname);
		SG_RuneLevelClear();
		return 0;
	}
	strncpy(sg_rune_level.mapname, mapname, sizeof(sg_rune_level.mapname) - 1U);
	sg_rune_level.mapname[sizeof(sg_rune_level.mapname) - 1U] = '\0';
	sg_rune_level.current = 1;
	gi.dprintf("slipgate: rune ready %s, cells %u portals %u capabilities %u "
		"bytes %lu\n", mapname,
		(unsigned int)sg_rune_level.artifact.complex.cell_count,
		(unsigned int)sg_rune_level.artifact.complex.portal_count,
		(unsigned int)sg_rune_level.artifact.movement.capability_count,
		(unsigned long)sg_rune_level.artifact.image_size);
	return 1;
}

static const sg_rune_field_t *FieldVariant(uint32_t destination_cell,
	uint32_t variant, const float *surcharge);

const sg_rune_field_t *SG_RuneLevelField(uint32_t destination_cell)
{
	return FieldVariant(destination_cell, 0U, NULL);
}

static const sg_rune_field_t *FieldVariant(uint32_t destination_cell,
	uint32_t variant, const float *surcharge)
{
	uint32_t index, victim = 0U;
	uint64_t oldest = UINT64_MAX;

	if (!sg_rune_level.current ||
		destination_cell >= sg_rune_level.artifact.complex.cell_count)
		return NULL;
	sg_rune_level.frame++;
	for (index = 0U; index < SG_RUNE_LEVEL_FIELDS; index++)
	{
		sg_rune_level_field_t *slot = &sg_rune_level.fields[index];

		if (slot->destination_cell == destination_cell && slot->variant == variant)
		{
			slot->last_used_frame = sg_rune_level.frame;
			return &slot->field;
		}
		if (slot->last_used_frame < oldest)
		{
			oldest = slot->last_used_frame;
			victim = index;
		}
	}
	{
		sg_rune_level_field_t *slot = &sg_rune_level.fields[victim];

		if (!SG_RuneFieldBuildWeighted(&slot->field, &sg_rune_level.router,
			destination_cell, surcharge))
		{
			slot->destination_cell = SG_RUNE_CX_INDEX_NONE;
			return NULL;
		}
		slot->destination_cell = destination_cell;
		slot->variant = variant;
		slot->last_used_frame = sg_rune_level.frame;
		return &slot->field;
	}
}

uint32_t SG_RuneLevelLocate(const float origin[3], int crouching,
	float *violation_out)
{
	if (!sg_rune_level.current)
	{
		if (violation_out)
			*violation_out = 0.0f;
		return SG_RUNE_CX_INDEX_NONE;
	}
	return SG_RuneLocate(&sg_rune_level.locator, origin,
		crouching ? SG_RUNE_MOVE_CROUCHING : SG_RUNE_MOVE_STANDING, 8.0f,
		violation_out);
}

static edict_t *FindBrushModel(int bmodel)
{
	char name[16];
	int i;

	snprintf(name, sizeof(name), "*%d", bmodel);
	for (i = 1; i < globals.num_edicts; i++)
	{
		edict_t *e = &g_edicts[i];

		if (e->inuse && e->model && !strcmp(e->model, name))
			return e;
	}
	return NULL;
}

static edict_t *FindPointEntity(const float origin[3], const char *classname)
{
	int i;
	edict_t *best = NULL;
	float best_distance = 64.0f;

	for (i = 1; i < globals.num_edicts; i++)
	{
		edict_t *e = &g_edicts[i];
		vec3_t delta;
		float distance;

		if (!e->inuse || !e->classname || strcmp(e->classname, classname))
			continue;
		VectorSubtract(e->s.origin, origin, delta);
		distance = VectorLength(delta);
		if (distance < best_distance)
		{
			best_distance = distance;
			best = e;
		}
	}
	return best;
}

edict_t *SG_RuneLevelMechanismEdict(uint32_t mechanism)
{
	const sg_rune_mech_t *record;
	edict_t *e = NULL;

	if (!sg_rune_level.current ||
		mechanism >= sg_rune_level.artifact.mechanisms.record_count)
		return NULL;
	if (!sg_rune_level.mechanism_edict)
	{
		sg_rune_level.mechanism_edict = calloc(
			(size_t)sg_rune_level.artifact.mechanisms.record_count, sizeof(int));
		if (!sg_rune_level.mechanism_edict)
			return NULL;
	}
	if (sg_rune_level.mechanism_edict[mechanism] > 0)
	{
		e = &g_edicts[sg_rune_level.mechanism_edict[mechanism]];
		if (e->inuse)
			return e;
		sg_rune_level.mechanism_edict[mechanism] = 0;
	}
	record = &sg_rune_level.artifact.mechanisms.records[mechanism];
	if (record->bmodel >= 0)
		e = FindBrushModel(record->bmodel);
	else if (record->kind == SG_RUNE_MECH_TELEPORTER)
		e = FindPointEntity(record->origin, "misc_teleporter");
	if (e)
		sg_rune_level.mechanism_edict[mechanism] = (int)(e - g_edicts);
	return e;
}


uint32_t SG_RuneLevelFire(uint32_t cell, uint32_t target)
{
	if (!sg_rune_level.current)
		return 0U;
	return SG_RuneFireFlags(&sg_rune_level.artifact.fires, cell, target);
}

/* ---- defend posts --------------------------------------------------------------- */

#define POST_APPROACH_SECONDS 8.0f    /* how far out an approach is watched */
#define POST_REACH_SECONDS 3.0f       /* how far from the flag a post may be */
#define POST_MIN_FROM_FLAG 96.0f
#define POST_MIN_APART 160.0f


static float Flat(const float *a, const float *b)
{
	float dx = a[0] - b[0], dy = a[1] - b[1];

	return sqrtf(dx * dx + dy * dy);
}

/* Coverage of the approach cells still uncovered from a candidate post, and
 * the weighted centre of what it covers. */
static float Coverage(uint32_t post, const float *weight, const uint8_t *covered,
	float centre_out[3])
{
	const sg_rune_fire_table_t *fires = &sg_rune_level.artifact.fires;
	const sg_rune_fire_cell_t *row = &fires->cells[post];
	float total = 0.0f, sum[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t k;

	for (k = 0U; k < row->count; k++)
	{
		const sg_rune_fire_t *record = &fires->records[row->first + k];
		const float *centre;

		if (!(record->flags & SG_RUNE_FIRE_LINE) || weight[record->target] <= 0.0f ||
			covered[record->target])
			continue;
		centre = &sg_rune_level.router.cell_center[record->target * 3U];
		total += weight[record->target];
		sum[0] += centre[0] * weight[record->target];
		sum[1] += centre[1] * weight[record->target];
		sum[2] += centre[2] * weight[record->target];
	}
	if (total > 0.0f)
	{
		centre_out[0] = sum[0] / total;
		centre_out[1] = sum[1] / total;
		centre_out[2] = sum[2] / total;
	}
	return total;
}

static void MarkCovered(uint32_t post, uint8_t *covered)
{
	const sg_rune_fire_table_t *fires = &sg_rune_level.artifact.fires;
	const sg_rune_fire_cell_t *row = &fires->cells[post];
	uint32_t k;

	for (k = 0U; k < row->count; k++)
		if (fires->records[row->first + k].flags & SG_RUNE_FIRE_LINE)
			covered[fires->records[row->first + k].target] = 1U;
}

static int BuildPosts(post_set_t *set, uint32_t flag_cell)
{
	const sg_rune_field_t *field = SG_RuneLevelField(flag_cell);
	uint32_t cell_count = sg_rune_level.artifact.complex.cell_count;
	const float *flag_centre = &sg_rune_level.router.cell_center[flag_cell * 3U];
	float *weight = NULL;
	uint8_t *covered = NULL;
	uint32_t cell;
	int slot, ok = 0;
	sg_rune_field_t from_flag;

	memset(set, 0, sizeof(*set));
	memset(&from_flag, 0, sizeof(from_flag));
	set->flag_cell = flag_cell;
	if (!field || sg_rune_level.artifact.fires.cell_count != cell_count)
	{
		if (sg_cv.debug && sg_cv.debug->value)
			gi.dprintf("SGPOST flag cell %u: no posts (field %s, fire rows %u of %u cells)\n",
				(unsigned int)flag_cell, field ? "ready" : "missing",
				(unsigned int)sg_rune_level.artifact.fires.cell_count,
				(unsigned int)cell_count);
		return 0;
	}
	weight = calloc(cell_count, sizeof(*weight));
	covered = calloc(cell_count, sizeof(*covered));
	if (!weight || !covered)
		goto done;
	/* A post must be reachable from the flag as well as cover it: a ledge
	 * that only drops to the flag is no post.  One forward field from the
	 * flag answers that for every candidate. */
	if (!SG_RuneFieldBuildFrom(&from_flag, &sg_rune_level.router, flag_cell))
		goto done;
	/* Approaches: the nearer to the flag, the more it matters to see. */
	for (cell = 0U; cell < cell_count; cell++)
	{
		float cost = field->cost[SG_RUNE_FIELD_STATE(cell, 0)];

		if (cost > 0.0f && cost <= POST_APPROACH_SECONDS)
			weight[cell] = 1.0f + (POST_APPROACH_SECONDS - cost) / POST_APPROACH_SECONDS;
	}
	for (slot = 0; slot < POST_SLOTS; slot++)
	{
		uint32_t best = SG_RUNE_CX_INDEX_NONE;
		float best_score = 0.0f, best_centre[3] = { 0.0f, 0.0f, 0.0f };
		int attempts = 0;
		uint32_t candidates = 0U, seeing = 0U;

		best = SG_RUNE_CX_INDEX_NONE;
		best_score = 0.0f;
		for (cell = 0U; cell < cell_count; cell++)
		{
			float cost = field->cost[SG_RUNE_FIELD_STATE(cell, 0)];
			float back = from_flag.cost[SG_RUNE_FIELD_STATE(cell, 0)];
			const float *centre = &sg_rune_level.router.cell_center[cell * 3U];
			float centre_of_cover[3] = { 0.0f, 0.0f, 0.0f }, score;
			int other;

			if (!(back < POST_REACH_SECONDS * 2.0f))
				continue;
			if (!(cost >= 0.0f && cost <= POST_REACH_SECONDS) ||
				!(sg_rune_level.artifact.complex.cells[cell].semantics &
					SG_RUNE_CX_CELL_SUPPORTED) ||
				(sg_rune_level.artifact.complex.cells[cell].semantics &
					(SG_RUNE_CX_CELL_WATER | SG_RUNE_CX_CELL_HAZARD)) ||
				Flat(centre, flag_centre) < POST_MIN_FROM_FLAG || covered[cell] == 2U)
				continue;
			for (other = 0; other < slot; other++)
				if (Flat(centre, &sg_rune_level.router.cell_center[set->post[other] * 3U]) <
					POST_MIN_APART)
					break;
			if (other < slot)
				continue;
			candidates++;
			score = Coverage(cell, weight, covered, centre_of_cover);
			if (score > 0.0f)
				seeing++;
			if (score > best_score)
			{
				best_score = score;
				best = cell;
				memcpy(best_centre, centre_of_cover, sizeof(best_centre));
			}
		}
		if (best == SG_RUNE_CX_INDEX_NONE)
		{
			if (sg_cv.debug && sg_cv.debug->value)
				gi.dprintf("SGPOST flag cell %u post %d: none (%u candidates within "
					"reach both ways, %u seeing an approach)\n", (unsigned int)flag_cell,
					slot, (unsigned int)candidates, (unsigned int)seeing);
			break;
		}
		(void)attempts;
		set->post[slot] = best;
		if (sg_cv.debug && sg_cv.debug->value)
			gi.dprintf("SGPOST flag cell %u post %d: cell %u at (%.0f %.0f %.0f) "
				"coverage %.1f, %.1f s from the flag\n", (unsigned int)flag_cell,
				slot, (unsigned int)best,
				sg_rune_level.router.cell_center[best * 3U],
				sg_rune_level.router.cell_center[best * 3U + 1U],
				sg_rune_level.router.cell_center[best * 3U + 2U], best_score,
				SG_RuneLevelField(best)->cost[SG_RUNE_FIELD_STATE(flag_cell, 0)]);
		memcpy(set->facing[slot], best_centre, sizeof(set->facing[slot]));
		MarkCovered(best, covered);
		set->valid = slot + 1;
	}
	ok = set->valid > 0;
done:
	SG_RuneFieldFree(&from_flag);
	free(weight);
	free(covered);
	return ok;
}

int SG_RuneLevelDefendPost(uint32_t flag_cell, int slot, float point_out[3],
	float facing_out[3], uint32_t *cell_out)
{
	post_set_t *set = NULL;
	int index;

	if (!sg_rune_level.current || slot < 0 || slot >= POST_SLOTS ||
		flag_cell >= sg_rune_level.artifact.complex.cell_count)
	{
		if (sg_cv.debug && sg_cv.debug->value && level.framenum % 50 == 0)
			gi.dprintf("SGPOST flag cell %u slot %d: refused (rune %d)\n",
				(unsigned int)flag_cell, slot, sg_rune_level.current ? 1 : 0);
		return 0;
	}
	for (index = 0; index < POST_FLAGS; index++)
		if (sg_posts[index].flag_cell == flag_cell && sg_posts[index].valid)
			set = &sg_posts[index];
	if (!set)
	{
		/* Build into the slot not holding the other flag. */
		set = &sg_posts[0];
		for (index = 0; index < POST_FLAGS; index++)
			if (!sg_posts[index].valid)
			{
				set = &sg_posts[index];
				break;
			}
		if (!BuildPosts(set, flag_cell))
			return 0;
	}
	if (set->valid <= 0)
		return 0;
	if (slot >= set->valid)
		slot = set->valid - 1;
	memcpy(point_out, &sg_rune_level.router.cell_center[set->post[slot] * 3U],
		3U * sizeof(float));
	if (facing_out)
		memcpy(facing_out, set->facing[slot], 3U * sizeof(float));
	if (cell_out)
		*cell_out = set->post[slot];
	return 1;
}

/* ---- exposure ------------------------------------------------------------------- */

#define EXPOSURE_SECONDS 0.25f     /* each cell entered under a defender's line costs this */
#define EXPOSURE_SETS 2

typedef struct exposure_s
{
	uint32_t flag_cell;
	float *surcharge;          /* per cell */
} exposure_t;

static exposure_t sg_exposure[EXPOSURE_SETS];

static void ExposeFrom(uint32_t post, float *surcharge)
{
	const sg_rune_fire_table_t *fires = &sg_rune_level.artifact.fires;
	const sg_rune_fire_cell_t *row;
	uint32_t k;

	if (post >= fires->cell_count)
		return;
	row = &fires->cells[post];
	for (k = 0U; k < row->count; k++)
		if (fires->records[row->first + k].flags & SG_RUNE_FIRE_LINE)
			surcharge[fires->records[row->first + k].target] = EXPOSURE_SECONDS;
}

static const float *Exposure(uint32_t enemy_flag_cell)
{
	uint32_t cell_count = sg_rune_level.artifact.complex.cell_count;
	exposure_t *set = NULL;
	int index, slot;

	for (index = 0; index < EXPOSURE_SETS; index++)
		if (sg_exposure[index].surcharge && sg_exposure[index].flag_cell == enemy_flag_cell)
			return sg_exposure[index].surcharge;
	for (index = 0; index < EXPOSURE_SETS; index++)
		if (!sg_exposure[index].surcharge)
		{
			set = &sg_exposure[index];
			break;
		}
	if (!set)
		set = &sg_exposure[0];
	free(set->surcharge);
	set->surcharge = calloc(cell_count ? cell_count : 1U, sizeof(float));
	if (!set->surcharge)
		return NULL;
	set->flag_cell = enemy_flag_cell;
	ExposeFrom(enemy_flag_cell, set->surcharge);
	for (slot = 0; slot < POST_SLOTS; slot++)
	{
		vec3_t point, facing;
		uint32_t post;

		if (!SG_RuneLevelDefendPost(enemy_flag_cell, slot, point, facing, NULL))
			break;
		post = SG_RuneLevelLocate(point, 0, NULL);
		if (post != SG_RUNE_CX_INDEX_NONE)
			ExposeFrom(post, set->surcharge);
	}
	/* The surcharge is borrowed by the representative rows: every floor
	 * cell that borrows a row is exposed as its representative is. */
	{
		const sg_rune_fire_table_t *fires = &sg_rune_level.artifact.fires;
		uint32_t cell;

		if (fires->cell_count == cell_count)
			for (cell = 0U; cell < cell_count; cell++)
			{
				uint32_t rep = fires->cells[cell].representative;

				if (rep != SG_RUNE_CX_INDEX_NONE && rep != cell && rep < cell_count)
					set->surcharge[cell] = set->surcharge[rep];
			}
	}
	return set->surcharge;
}

const sg_rune_field_t *SG_RuneLevelFieldExposed(uint32_t destination_cell,
	uint32_t enemy_flag_cell)
{
	const float *surcharge;
	int index;

	if (!sg_rune_level.current ||
		enemy_flag_cell >= sg_rune_level.artifact.complex.cell_count ||
		sg_rune_level.artifact.fires.cell_count == 0U)
		return SG_RuneLevelField(destination_cell);
	surcharge = Exposure(enemy_flag_cell);
	if (!surcharge)
		return SG_RuneLevelField(destination_cell);
	for (index = 0; index < EXPOSURE_SETS; index++)
		if (sg_exposure[index].surcharge == surcharge)
			return FieldVariant(destination_cell, 1U + (uint32_t)index, surcharge);
	return SG_RuneLevelField(destination_cell);
}

void SG_RuneLevelExposureClear(void)
{
	int index;

	for (index = 0; index < EXPOSURE_SETS; index++)
	{
		free(sg_exposure[index].surcharge);
		sg_exposure[index].surcharge = NULL;
		sg_exposure[index].flag_cell = SG_RUNE_CX_INDEX_NONE;
	}
}
