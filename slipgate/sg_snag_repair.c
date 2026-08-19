/* sg_snag_repair.c -- strict, unlabelled map repair reader.
 *
 * This is deliberately outside the RUNE and sidecar codecs.  It only changes
 * field-flood preference and never graph records, proofs, deadlines, or live
 * state.  Its fixed arrays are reset and filled at level setup; there is no
 * frame allocation or input parsing after setup.
 */
#include "../g_local.h"
#include "sg_hooks.h"
#include "sg_identity.h"
#include "sg_local.h"
#include "sg_snag_repair.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SG_SNAG_MAX_REPAIRS 64
#define SG_SNAG_MAX_LINKS 262144
#define SG_SNAG_RADIUS_MIN 1.0f
#define SG_SNAG_RADIUS_MAX 512.0f
#define SG_SNAG_COORD_LIMIT 65536.0f
#define SG_SNAG_SURCHARGE_MAX 60000
#define SG_SNAG_EVIDENCE_MAX 1000000U
#define SG_SNAG_DURATION_MAX 86400000U

static int sg_snag_seed[SG_MAX_SEEDS];
static int sg_snag_link[SG_SNAG_MAX_LINKS];

typedef struct sg_snag_record_s
{
	float x, y, z, radius;
	unsigned evidence_count;
	unsigned duration_ms;
	int surcharge_ms;
} sg_snag_record_t;

static int Snag_Number(const char *text, double *out)
{
	char *end;
	double value;

	if (!text || !*text)
		return 0;
	errno = 0;
	value = strtod(text, &end);
	if (errno == ERANGE || end == text || *end != '\0' || !isfinite(value))
		return 0;
	*out = value;
	return 1;
}

static int Snag_U32(const char *text, uint32_t *out)
{
	double value;

	if (!Snag_Number(text, &value) || value < 0.0 ||
	    value > (double)UINT32_MAX || floor(value) != value)
		return 0;
	*out = (uint32_t)value;
	return 1;
}

static int Snag_Float(const char *text, float minimum, float maximum,
	float *out)
{
	double value;

	if (!Snag_Number(text, &value) || value < minimum || value > maximum ||
	    value < -FLT_MAX || value > FLT_MAX)
		return 0;
	*out = (float)value;
	return isfinite(*out);
}

static int Snag_Line(FILE *file, char *line, size_t line_size)
{
	size_t length;

	if (!fgets(line, (int)line_size, file))
		return feof(file) ? -1 : 0;
	length = strlen(line);
	if (length == 0 || line[length - 1] != '\n')
		return 0;
	line[length - 1] = '\0';
	return line[0] != '\0';
}

static int Snag_Field(FILE *file, const char *name, char *value,
	size_t value_size)
{
	char line[256];
	size_t name_length = strlen(name);

	if (Snag_Line(file, line, sizeof(line)) != 1 ||
	    strncmp(line, name, name_length) != 0 || line[name_length] != ' ' ||
	    line[name_length + 1] == '\0' ||
	    strlen(line + name_length + 1) >= value_size)
		return 0;
	strcpy(value, line + name_length + 1);
	return strchr(value, ' ') == NULL;
}

static int Snag_Identity(FILE *file, const rune_identity_t *identity)
{
	char value[128];
	uint32_t u32;
	float f;

	if (!Snag_Field(file, "map", value, sizeof(value)) ||
	    strcmp(value, identity->map_name) != 0)
		return 0;
	if (!Snag_Field(file, "bsp_checksum", value, sizeof(value)) ||
	    !Snag_U32(value, &u32) || u32 != identity->bsp_checksum)
		return 0;
	if (!Snag_Field(file, "entity_crc", value, sizeof(value)) ||
	    !Snag_U32(value, &u32) || u32 != identity->entity_crc32)
		return 0;
	if (!Snag_Field(file, "physics_flags", value, sizeof(value)) ||
	    !Snag_U32(value, &u32) || u32 != identity->physics_flags)
		return 0;
	if (!Snag_Field(file, "gravity", value, sizeof(value)) ||
	    !Snag_Float(value, -SG_SNAG_COORD_LIMIT, SG_SNAG_COORD_LIMIT, &f) ||
	    f != identity->gravity)
		return 0;
	if (!Snag_Field(file, "airaccelerate", value, sizeof(value)) ||
	    !Snag_Float(value, -SG_SNAG_COORD_LIMIT, SG_SNAG_COORD_LIMIT, &f) ||
	    f != identity->airaccelerate)
		return 0;
	if (!Snag_Field(file, "maxvelocity", value, sizeof(value)) ||
	    !Snag_Float(value, 0.0f, SG_SNAG_COORD_LIMIT, &f) ||
	    f != identity->maxvelocity)
		return 0;
	if (!Snag_Field(file, "pmove_ms", value, sizeof(value)) ||
	    !Snag_U32(value, &u32) || u32 != identity->pmove_substep_ms)
		return 0;
	if (!Snag_Field(file, "frame_ms", value, sizeof(value)) ||
	    !Snag_U32(value, &u32) || u32 != identity->server_frame_ms)
		return 0;
	if (!Snag_Field(file, "host_physics_id", value, sizeof(value)) ||
	    !Snag_U32(value, &u32) || u32 != identity->host_physics_id)
		return 0;
	return 1;
}

static int Snag_ParseRepair(const char *line, sg_snag_record_t *out)
{
	char extra;
	int matched;
	unsigned evidence, duration;
	int surcharge;

	matched = sscanf(line, "repair %f %f %f %f %u %u %d %c", &out->x,
		&out->y, &out->z, &out->radius, &evidence, &duration, &surcharge,
		&extra);
	if (matched != 7 || !isfinite(out->x) || !isfinite(out->y) ||
	    !isfinite(out->z) || !isfinite(out->radius) ||
	    fabsf(out->x) > SG_SNAG_COORD_LIMIT ||
	    fabsf(out->y) > SG_SNAG_COORD_LIMIT ||
	    fabsf(out->z) > SG_SNAG_COORD_LIMIT ||
	    out->radius < SG_SNAG_RADIUS_MIN || out->radius > SG_SNAG_RADIUS_MAX ||
	    evidence == 0 || evidence > SG_SNAG_EVIDENCE_MAX || duration == 0 ||
	    duration > SG_SNAG_DURATION_MAX || surcharge < 0 ||
	    surcharge > SG_SNAG_SURCHARGE_MAX)
		return 0;
	out->evidence_count = evidence;
	out->duration_ms = duration;
	out->surcharge_ms = surcharge;
	return 1;
}

static int Snag_Duplicate(const sg_snag_record_t *records, int count,
	const sg_snag_record_t *candidate)
{
	int i;

	for (i = 0; i < count; i++)
		if (records[i].x == candidate->x && records[i].y == candidate->y &&
		    records[i].z == candidate->z &&
		    records[i].radius == candidate->radius)
			return 1;
	return 0;
}

static int Snag_Apply(const rune_t *rune, const sg_snag_record_t *record)
{
	int i, matches = 0, horizontal_only = 0, matched_seed = -1;
	float radius2 = record->radius * record->radius;
	int seed_part = record->surcharge_ms / 2;
	int link_part = record->surcharge_ms - seed_part;

	for (i = 0; i < rune->hdr.num_seeds && i < SG_MAX_SEEDS; i++)
	{
		const rune_seed_t *seed = &rune->seeds[i];
		float dx, dy, dz;

		if (seed->flags & (RSF_TOMBSTONE | RSF_WATER))
			continue;
		dx = seed->origin[0] - record->x;
		dy = seed->origin[1] - record->y;
		dz = seed->origin[2] - record->z;
		if (dx * dx + dy * dy <= radius2 && dz * dz > radius2)
			horizontal_only++;
		if (dx * dx + dy * dy + dz * dz <= radius2)
		{
			matched_seed = i;
			matches++;
		}
	}
	/* A coordinate that sees another grounded floor at this horizontal point
	 * but cannot place it in Z is ambiguous, never a nearest-seed snap. */
	if (horizontal_only != 0)
	{
		if (sg_host.dprint)
			sg_host.dprint("slipgate: snag repair rejected: vertical ambiguity\n");
		return 0;
	}
	if (matches != 1)
	{
		if (sg_host.dprint)
			sg_host.dprint("slipgate: snag repair rejected: ambiguous coordinate matches %d grounded seeds\n",
			matches);
		return 0;
	}
	if (sg_snag_seed[matched_seed] > SG_SNAG_SURCHARGE_MAX - seed_part)
		return 0;
	sg_snag_seed[matched_seed] += seed_part;
	for (i = 0; i < rune->hdr.num_links && i < SG_SNAG_MAX_LINKS; i++)
	{
		int from = rune->links[i].from;

		if (from == matched_seed)
		{
			if (sg_snag_link[i] > SG_SNAG_SURCHARGE_MAX - link_part)
				return 0;
			sg_snag_link[i] += link_part;
		}
	}
	return 1;
}

void SG_SnagRepairClear(void)
{
	memset(sg_snag_seed, 0, sizeof(sg_snag_seed));
	memset(sg_snag_link, 0, sizeof(sg_snag_link));
}

int SG_SnagRepairSeedSurcharge(int seed)
{
	return seed >= 0 && seed < SG_MAX_SEEDS ? sg_snag_seed[seed] : 0;
}

int SG_SnagRepairLinkSurcharge(int link)
{
	return link >= 0 && link < SG_SNAG_MAX_LINKS ? sg_snag_link[link] : 0;
}

qboolean SG_SnagRepairLoadFile(const rune_t *rune, const char *path)
{
	FILE *file;
	char line[256];
	sg_snag_record_t records[SG_SNAG_MAX_REPAIRS];
	int count = 0, i;

	SG_SnagRepairClear();
	if (!rune || !path || !path[0] || !rune->seeds || !rune->links ||
	    rune->hdr.num_seeds <= 0 || rune->hdr.num_seeds > SG_MAX_SEEDS ||
	    rune->hdr.num_links < 0 || rune->hdr.num_links > SG_SNAG_MAX_LINKS)
		return false;
	file = fopen(path, "rb");
	if (!file)
		return errno == ENOENT;
	if (!Snag_Identity(file, &rune->artifact.identity))
		goto fail;
	for (;;)
	{
		int line_status = Snag_Line(file, line, sizeof(line));

		if (line_status < 0)
			break;
		if (line_status == 0)
			goto fail;
		if (count >= SG_SNAG_MAX_REPAIRS ||
		    !Snag_ParseRepair(line, &records[count]) ||
		    Snag_Duplicate(records, count, &records[count]))
			goto fail;
		count++;
	}
	if (ferror(file) || count == 0)
		goto fail;
	for (i = 0; i < count; i++)
		if (!Snag_Apply(rune, &records[i]))
			goto fail;
	fclose(file);
	return true;
fail:
	fclose(file);
	SG_SnagRepairClear();
	return false;
}

qboolean SG_SnagRepairLoadForLevel(const rune_t *rune, const char *game_dir)
{
	sg_level_identity_t level_identity;
	char path[MAX_OSPATH];
	int written;

	if (!rune || !game_dir || !game_dir[0] ||
	    SG_LevelIdentitySnapshot(rune->artifact.identity.map_name,
		&level_identity) != SG_IDENTITY_OK ||
	    level_identity.bsp_checksum != rune->artifact.identity.bsp_checksum ||
	    level_identity.entity_crc32 != rune->artifact.identity.entity_crc32 ||
	    level_identity.host_physics_id != rune->artifact.identity.host_physics_id)
		return false;
	written = snprintf(path, sizeof(path), "%s/maps/%s.snag", game_dir,
		rune->artifact.identity.map_name);
	if (written < 0 || (size_t)written >= sizeof(path))
		return false;
	return SG_SnagRepairLoadFile(rune, path);
}
