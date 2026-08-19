/* sg_snag_repair.c -- strict, RUNE-seed-bound map repair reader.
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
#include "sg_rune_file.h"
#include "sg_snag_repair.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SG_SNAG_MAX_REPAIRS 512
#define SG_SNAG_MAX_LINKS 262144
#define SG_SNAG_COORD_LIMIT 65536.0f
#define SG_SNAG_SURCHARGE_MAX 60000
#define SG_SNAG_EVIDENCE_MAX 1000000U
#define SG_SNAG_DURATION_MAX 86400000U
#define SG_SNAG_FORMAT 2U
#define SG_SNAG_SHA256_HEX_BYTES 64U
#define SG_SNAG_CAPTURE_BYTES ((SG_SNAG_MAX_REPAIRS + 32U) * 256U)

static int sg_snag_seed[SG_MAX_SEEDS];
static int sg_snag_link[SG_SNAG_MAX_LINKS];

typedef struct sg_snag_record_s
{
	uint32_t seed;
	float x, y, z;
	unsigned evidence_count;
	unsigned duration_ms;
	int surcharge_ms;
} sg_snag_record_t;

typedef struct sg_snag_capture_s
{
	unsigned char *bytes;
	size_t length;
	size_t capacity;
} sg_snag_capture_t;

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
	uint64_t value = 0;
	size_t i;

	if (!text || !text[0] || (text[0] == '0' && text[1] != '\0'))
		return 0;
	for (i = 0; text[i] != '\0'; i++)
	{
		if (text[i] < '0' || text[i] > '9')
			return 0;
		value = value * 10U + (uint64_t)(text[i] - '0');
		if (value > UINT32_MAX)
			return 0;
	}
	*out = (uint32_t)value;
	return 1;
}

static int Snag_SHA256(const char *text)
{
	size_t i;

	if (!text || strlen(text) != SG_SNAG_SHA256_HEX_BYTES)
		return 0;
	for (i = 0; i < SG_SNAG_SHA256_HEX_BYTES; i++)
		if (!((text[i] >= '0' && text[i] <= '9') ||
		      (text[i] >= 'a' && text[i] <= 'f')))
			return 0;
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

static int Snag_Line(FILE *file, char *line, size_t line_size,
	sg_snag_capture_t *capture)
{
	size_t length;

	if (!fgets(line, (int)line_size, file))
		return feof(file) ? -1 : 0;
	length = strlen(line);
	if (length == 0 || line[length - 1] != '\n')
		return 0;
	if (capture)
	{
		if (!capture->bytes || length > capture->capacity - capture->length)
			return 0;
		memcpy(capture->bytes + capture->length, line, length);
		capture->length += length;
	}
	line[length - 1] = '\0';
	return line[0] != '\0';
}

static int Snag_Field(FILE *file, const char *name, char *value,
	size_t value_size, sg_snag_capture_t *capture)
{
	char line[256];
	size_t name_length = strlen(name);

	if (Snag_Line(file, line, sizeof(line), capture) != 1 ||
	    strncmp(line, name, name_length) != 0 || line[name_length] != ' ' ||
	    line[name_length + 1] == '\0' ||
	    strlen(line + name_length + 1) >= value_size)
		return 0;
	strcpy(value, line + name_length + 1);
	return strchr(value, ' ') == NULL;
}

static int Snag_SameFloatBits(float left, float right);

static int Snag_ArtifactFloat(FILE *file, const char *name, float expected,
	float minimum, float maximum, sg_snag_capture_t *capture)
{
	char value[128];
	char canonical[128];
	float parsed;
	int written;

	if (!Snag_Field(file, name, value, sizeof(value), capture) ||
	    !Snag_Float(value, minimum, maximum, &parsed) ||
	    !Snag_SameFloatBits(parsed, expected))
		return 0;
	written = snprintf(canonical, sizeof(canonical), "%.9g", (double)expected);
	return written > 0 && (size_t)written < sizeof(canonical) &&
	       strcmp(value, canonical) == 0;
}

static int Snag_Artifact(FILE *file, const rune_t *rune,
	uint32_t *repair_count, char evidence_sha256[65],
	sg_snag_capture_t *capture)
{
	char value[128];
	uint32_t u32;
	const rune_artifact_t *artifact;
	const rune_identity_t *identity;

	if (!rune || !repair_count || !evidence_sha256 ||
	    !Snag_SHA256(rune->encoded_sha256))
		return 0;
	artifact = &rune->artifact;
	identity = &artifact->identity;

	if (!Snag_Field(file, "snag_format", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != SG_SNAG_FORMAT)
		return 0;
	if (!Snag_Field(file, "map", value, sizeof(value), capture) ||
	    strcmp(value, identity->map_name) != 0)
		return 0;
	if (!Snag_Field(file, "bsp_checksum", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != identity->bsp_checksum)
		return 0;
	if (!Snag_Field(file, "entity_crc", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != identity->entity_crc32)
		return 0;
	if (!Snag_Field(file, "physics_flags", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != identity->physics_flags)
		return 0;
	if (!Snag_ArtifactFloat(file, "gravity", identity->gravity,
	    -SG_SNAG_COORD_LIMIT, SG_SNAG_COORD_LIMIT, capture))
		return 0;
	if (!Snag_ArtifactFloat(file, "airaccelerate", identity->airaccelerate,
	    -SG_SNAG_COORD_LIMIT, SG_SNAG_COORD_LIMIT, capture))
		return 0;
	if (!Snag_ArtifactFloat(file, "maxvelocity", identity->maxvelocity,
	    0.0f, SG_SNAG_COORD_LIMIT, capture))
		return 0;
	if (!Snag_Field(file, "pmove_ms", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != identity->pmove_substep_ms)
		return 0;
	if (!Snag_Field(file, "frame_ms", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != identity->server_frame_ms)
		return 0;
	if (!Snag_Field(file, "host_physics_id", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != identity->host_physics_id)
		return 0;
	if (!Snag_Field(file, "rune_payload_crc", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != artifact->payload_crc32)
		return 0;
	if (!Snag_Field(file, "rune_header_crc", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != artifact->header_crc32)
		return 0;
	if (!Snag_Field(file, "rune_action_contract_crc", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != artifact->action_contract_crc32)
		return 0;
	if (!Snag_Field(file, "rune_mechanism_contract_crc", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != artifact->mechanism_contract_crc32)
		return 0;
	if (!Snag_Field(file, "rune_num_seeds", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != artifact->num_seeds)
		return 0;
	if (!Snag_Field(file, "rune_num_links", value, sizeof(value), capture) ||
	    !Snag_U32(value, &u32) || u32 != artifact->num_links)
		return 0;
	if (!Snag_Field(file, "rune_sha256", value, sizeof(value), capture) ||
	    !Snag_SHA256(value) || strcmp(value, rune->encoded_sha256) != 0)
		return 0;
	if (!Snag_Field(file, "evidence_sha256", value, sizeof(value), capture) ||
	    !Snag_SHA256(value))
		return 0;
	strcpy(evidence_sha256, value);
	if (!Snag_Field(file, "repairs", value, sizeof(value), capture) ||
	    !Snag_U32(value, repair_count) ||
	    *repair_count > SG_SNAG_MAX_REPAIRS)
		return 0;
	return 1;
}

static int Snag_ParseRepair(const char *line, sg_snag_record_t *out)
{
	char extra;
	char canonical[256];
	int matched;
	int written;
	unsigned evidence, duration;
	int surcharge;

	matched = sscanf(line, "repair %u %f %f %f %u %u %d %c", &out->seed,
		&out->x, &out->y, &out->z, &evidence, &duration, &surcharge, &extra);
	if (matched != 7 || !isfinite(out->x) || !isfinite(out->y) ||
	    !isfinite(out->z) ||
	    fabsf(out->x) > SG_SNAG_COORD_LIMIT ||
	    fabsf(out->y) > SG_SNAG_COORD_LIMIT ||
	    fabsf(out->z) > SG_SNAG_COORD_LIMIT ||
	    evidence == 0 || evidence > SG_SNAG_EVIDENCE_MAX || duration == 0 ||
	    duration > SG_SNAG_DURATION_MAX || surcharge < 0 ||
	    surcharge > SG_SNAG_SURCHARGE_MAX)
		return 0;
	out->evidence_count = evidence;
	out->duration_ms = duration;
	out->surcharge_ms = surcharge;
	written = snprintf(canonical, sizeof(canonical),
		"repair %u %.3f %.3f %.3f %u %u %d", out->seed,
		out->x, out->y, out->z, out->evidence_count, out->duration_ms,
		out->surcharge_ms);
	return written > 0 && (size_t)written < sizeof(canonical) &&
	       strcmp(line, canonical) == 0;
}

static int Snag_Duplicate(const sg_snag_record_t *records, int count,
	const sg_snag_record_t *candidate)
{
	int i;

	for (i = 0; i < count; i++)
		if (records[i].seed == candidate->seed)
			return 1;
	return 0;
}

static int Snag_SameFloatBits(float left, float right)
{
	uint32_t left_bits;
	uint32_t right_bits;

	memcpy(&left_bits, &left, sizeof(left_bits));
	memcpy(&right_bits, &right, sizeof(right_bits));
	return left_bits == right_bits;
}

static int Snag_Apply(const rune_t *rune, const sg_snag_record_t *record)
{
	int i, matched_seed, has_outgoing = 0;
	const rune_seed_t *seed;
	int seed_part = record->surcharge_ms / 2;
	int link_part = record->surcharge_ms - seed_part;

	if (record->seed >= (uint32_t)rune->hdr.num_seeds ||
	    record->seed >= SG_MAX_SEEDS)
		return 0;
	matched_seed = (int)record->seed;
	seed = &rune->seeds[matched_seed];
	if ((seed->flags & (RSF_TOMBSTONE | RSF_WATER)) ||
	    !Snag_SameFloatBits(seed->origin[0], record->x) ||
	    !Snag_SameFloatBits(seed->origin[1], record->y) ||
	    !Snag_SameFloatBits(seed->origin[2], record->z))
		return 0;
	for (i = 0; i < rune->hdr.num_links && i < SG_SNAG_MAX_LINKS; i++)
		if (rune->links[i].from == matched_seed)
		{
			has_outgoing = 1;
			break;
		}
	if (!has_outgoing)
		return 0;
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
	FILE *file = NULL;
	char line[256];
	char evidence_sha256[65];
	char snag_sha256[65];
	sg_snag_record_t records[SG_SNAG_MAX_REPAIRS];
	sg_snag_capture_t capture;
	uint32_t expected_count;
	int count = 0, i;

	memset(&capture, 0, sizeof(capture));
	SG_SnagRepairClear();
	if (!rune || !path || !path[0] || !rune->seeds || !rune->links ||
	    rune->hdr.num_seeds <= 0 || rune->hdr.num_seeds > SG_MAX_SEEDS ||
	    rune->hdr.num_links < 0 || rune->hdr.num_links > SG_SNAG_MAX_LINKS)
		return false;
	capture.bytes = malloc(SG_SNAG_CAPTURE_BYTES);
	if (!capture.bytes)
		return false;
	capture.capacity = SG_SNAG_CAPTURE_BYTES;
	file = fopen(path, "rb");
	if (!file)
		goto fail;
	if (!Snag_Artifact(file, rune, &expected_count, evidence_sha256,
	    &capture))
		goto fail;
	while ((uint32_t)count < expected_count)
	{
		int line_status = Snag_Line(file, line, sizeof(line), &capture);

		if (line_status != 1)
			goto fail;
		if (!Snag_ParseRepair(line, &records[count]) ||
		    Snag_Duplicate(records, count, &records[count]))
			goto fail;
		count++;
	}
	/* The declared count owns the complete tail.  A trailing blank, comment,
	 * repair, or arbitrary byte is not an ignorable extension. */
	if (Snag_Line(file, line, sizeof(line), &capture) != -1 || ferror(file))
		goto fail;
	SG_RuneFileSHA256Buffer(capture.bytes, capture.length, snag_sha256);
	if (fclose(file) != 0)
	{
		file = NULL;
		goto fail;
	}
	file = NULL;
	for (i = 0; i < count; i++)
		if (!Snag_Apply(rune, &records[i]))
			goto fail;
	if (sg_host.dprint)
		sg_host.dprint("slipgate: snag ready map=%s repairs=%u "
			"rune_sha256=%s evidence_sha256=%s snag_sha256=%s\n",
			rune->artifact.identity.map_name, expected_count,
			rune->encoded_sha256, evidence_sha256, snag_sha256);
	free(capture.bytes);
	return true;
fail:
	if (file)
		fclose(file);
	free(capture.bytes);
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
