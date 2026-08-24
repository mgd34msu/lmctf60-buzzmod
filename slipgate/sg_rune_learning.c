/* Canonical parser for human route nominations. Geometry remains untrusted. */
#include "../q_shared.h"
#include "sg_rune_learning.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEARNING_FORMAT_DRY 1U
#define LEARNING_FORMAT_TYPED 2U
#define LEARNING_LINE_BYTES 512U
#define LEARNING_COORD_Q8_MIN (-32768)
#define LEARNING_COORD_Q8_MAX 32767

static int LearningSHA256(const char *text)
{
	size_t index;

	if (!text || strlen(text) != 64U)
		return 0;
	for (index = 0; index < 64U; index++)
		if (!((text[index] >= '0' && text[index] <= '9') ||
		      (text[index] >= 'a' && text[index] <= 'f')))
			return 0;
	return 1;
}

static int LearningLine(FILE *file, char *line, size_t line_size)
{
	size_t length;

	if (!fgets(line, (int)line_size, file))
		return feof(file) ? -1 : 0;
	length = strlen(line);
	if (length == 0U || line[length - 1U] != '\n')
		return 0;
	line[length - 1U] = '\0';
	return line[0] != '\0';
}

static int LearningField(FILE *file, const char *name,
	char *value, size_t value_size)
{
	char line[LEARNING_LINE_BYTES];
	size_t name_length = strlen(name);

	if (LearningLine(file, line, sizeof(line)) != 1 ||
	    strncmp(line, name, name_length) != 0 ||
	    line[name_length] != ' ' || line[name_length + 1U] == '\0' ||
	    strlen(line + name_length + 1U) >= value_size)
		return 0;
	strcpy(value, line + name_length + 1U);
	return strchr(value, ' ') == NULL;
}

static int LearningU64(const char *text, uint64_t *out)
{
	uint64_t value = 0U;
	size_t index;

	if (!text || !text[0] || (text[0] == '0' && text[1] != '\0'))
		return 0;
	for (index = 0; text[index] != '\0'; index++)
	{
		uint64_t digit;

		if (text[index] < '0' || text[index] > '9')
			return 0;
		digit = (uint64_t)(text[index] - '0');
		if (value > (UINT64_MAX - digit) / UINT64_C(10))
			return 0;
		value = value * UINT64_C(10) + digit;
	}
	*out = value;
	return 1;
}

static int LearningU32(const char *text, uint32_t *out)
{
	uint64_t value;

	if (!LearningU64(text, &value) || value > UINT32_MAX)
		return 0;
	*out = (uint32_t)value;
	return 1;
}

static int LearningI32(const char *text, int32_t *out)
{
	uint64_t magnitude = 0U;
	uint64_t limit;
	size_t index = 0U;
	int negative = 0;

	if (!text || !text[0])
		return 0;
	if (text[index] == '-')
	{
		negative = 1;
		index++;
	}
	if (!text[index] || (text[index] == '0' && text[index + 1U] != '\0'))
		return 0;
	limit = negative ? UINT64_C(2147483648) : UINT64_C(2147483647);
	for (; text[index] != '\0'; index++)
	{
		uint64_t digit;

		if (text[index] < '0' || text[index] > '9')
			return 0;
		digit = (uint64_t)(text[index] - '0');
		if (magnitude > (limit - digit) / UINT64_C(10))
			return 0;
		magnitude = magnitude * UINT64_C(10) + digit;
	}
	if (negative && magnitude == 0U)
		return 0;
	if (negative && magnitude == UINT64_C(2147483648))
		*out = INT32_MIN;
	else
		*out = negative ? -(int32_t)magnitude : (int32_t)magnitude;
	return 1;
}

static int LearningI16(const char *text, int16_t *out)
{
	int32_t value;

	if (!LearningI32(text, &value) || value < INT16_MIN || value > INT16_MAX)
		return 0;
	*out = (int16_t)value;
	return 1;
}

static int LearningToken(const char **cursor, char *token,
	size_t token_size, int last)
{
	const char *end;
	size_t length;

	if (!cursor || !*cursor || !(*cursor)[0])
		return 0;
	end = strchr(*cursor, ' ');
	if ((last && end) || (!last && !end))
		return 0;
	if (last)
		end = *cursor + strlen(*cursor);
	length = (size_t)(end - *cursor);
	if (length == 0U || length >= token_size)
		return 0;
	memcpy(token, *cursor, length);
	token[length] = '\0';
	*cursor = last ? end : end + 1;
	return 1;
}

static int LearningSameFloat(float left, float right)
{
	uint32_t left_bits;
	uint32_t right_bits;

	memcpy(&left_bits, &left, sizeof(left_bits));
	memcpy(&right_bits, &right, sizeof(right_bits));
	return left_bits == right_bits;
}

static int LearningArtifactFloat(FILE *file, const char *name, float expected)
{
	char value[128];
	char canonical[128];
	char *end;
	double parsed_double;
	float parsed;
	int written;

	if (!LearningField(file, name, value, sizeof(value)))
		return 0;
	errno = 0;
	parsed_double = strtod(value, &end);
	if (errno == ERANGE || end == value || *end != '\0' ||
	    !isfinite(parsed_double) || parsed_double < -FLT_MAX ||
	    parsed_double > FLT_MAX)
		return 0;
	parsed = (float)parsed_double;
	if (!LearningSameFloat(parsed, expected))
		return 0;
	written = snprintf(canonical, sizeof(canonical), "%.9g", (double)expected);
	return written > 0 && (size_t)written < sizeof(canonical) &&
	       strcmp(value, canonical) == 0;
}

static int LearningArtifactU32(FILE *file, const char *name,
	uint32_t expected)
{
	char value[128];
	uint32_t parsed;

	return LearningField(file, name, value, sizeof(value)) &&
	       LearningU32(value, &parsed) && parsed == expected;
}

static int LearningArtifact(const rune_t *source, FILE *file,
	uint32_t *format, uint32_t *candidate_count, char trace_sha256[65],
	char replay_sha256[65])
{
	const rune_artifact_t *artifact;
	const rune_identity_t *identity;
	char value[128];
	uint32_t parsed;

	if (!source || !source->seeds || !format || !candidate_count ||
	    !trace_sha256 || !replay_sha256 ||
	    source->artifact.magic != RUNE_ARTIFACT_MAGIC ||
	    source->artifact.route_contract != RUNE_ROUTE_CONTRACT_LOCAL_ONLY ||
	    source->artifact.num_seeds == 0U ||
	    source->artifact.num_seeds > RUNE_MAX_SEEDS ||
	    source->hdr.num_seeds != (int)source->artifact.num_seeds ||
	    source->hdr.num_links != (int)source->artifact.num_links ||
	    !LearningSHA256(source->encoded_sha256))
		return 0;
	artifact = &source->artifact;
	identity = &artifact->identity;
	if (!LearningField(file, "rlearn_format", value, sizeof(value)) ||
	    !LearningU32(value, &parsed) ||
	    (parsed != LEARNING_FORMAT_DRY &&
	     parsed != LEARNING_FORMAT_TYPED))
		return 0;
	*format = parsed;
	if (!LearningField(file, "map", value, sizeof(value)) ||
	    strcmp(value, identity->map_name) != 0 ||
	    !LearningArtifactU32(file, "bsp_checksum", identity->bsp_checksum) ||
	    !LearningArtifactU32(file, "entity_crc", identity->entity_crc32) ||
	    !LearningArtifactU32(file, "physics_flags", identity->physics_flags) ||
	    !LearningArtifactFloat(file, "gravity", identity->gravity) ||
	    !LearningArtifactFloat(file, "airaccelerate",
	        identity->airaccelerate) ||
	    !LearningArtifactFloat(file, "maxvelocity", identity->maxvelocity) ||
	    !LearningArtifactU32(file, "pmove_ms", identity->pmove_substep_ms) ||
	    !LearningArtifactU32(file, "frame_ms", identity->server_frame_ms) ||
	    !LearningArtifactU32(file, "host_physics_id",
	        identity->host_physics_id) ||
	    !LearningArtifactU32(file, "source_route_contract",
	        RUNE_ROUTE_CONTRACT_LOCAL_ONLY) ||
	    !LearningArtifactU32(file, "rune_payload_crc",
	        artifact->payload_crc32) ||
	    !LearningArtifactU32(file, "rune_header_crc",
	        artifact->header_crc32) ||
	    !LearningArtifactU32(file, "rune_action_contract_crc",
	        artifact->action_contract_crc32) ||
	    !LearningArtifactU32(file, "rune_mechanism_contract_crc",
	        artifact->mechanism_contract_crc32) ||
	    !LearningArtifactU32(file, "rune_num_seeds", artifact->num_seeds) ||
	    !LearningArtifactU32(file, "rune_num_links", artifact->num_links) ||
	    !LearningArtifactU32(file, "rune_num_mechanism_nodes",
	        artifact->num_mechanism_nodes) ||
	    !LearningArtifactU32(file, "rune_num_mechanism_edges",
	        artifact->num_mechanism_edges) ||
	    !LearningArtifactU32(file, "rune_num_inventory_edges",
	        artifact->num_inventory_edges) ||
	    !LearningArtifactU32(file, "rune_num_mechanism_plans",
	        artifact->num_mechanism_plans) ||
	    !LearningArtifactU32(file, "rune_string_bytes",
	        artifact->string_bytes) ||
	    !LearningField(file, "rune_sha256", value, sizeof(value)) ||
	    !LearningSHA256(value) || strcmp(value, source->encoded_sha256) != 0 ||
	    !LearningField(file, "trace_sha256", trace_sha256, 65U) ||
	    !LearningSHA256(trace_sha256) ||
	    !LearningField(file, "replay_sha256", replay_sha256, 65U) ||
	    !LearningSHA256(replay_sha256) ||
	    !LearningField(file, "candidates", value, sizeof(value)) ||
	    !LearningU32(value, &parsed) ||
	    parsed > SG_RUNE_LEARNING_MAX_CANDIDATES)
		return 0;
	*candidate_count = parsed;
	return 1;
}

static int LearningCandidateLine(const char *line,
	sg_rune_learning_candidate_t *candidate)
{
	char canonical[LEARNING_LINE_BYTES];
	char fields[15][32];
	const char *cursor;
	uint32_t from, to, hint, has_waypoint;
	int32_t from_x, from_y, from_z, to_x, to_y, to_z;
	int32_t waypoint_x, waypoint_y, waypoint_z;
	uint64_t first_sequence, last_sequence;
	int index;
	int written;

	if (strncmp(line, "candidate ", 10U) != 0)
		return 0;
	cursor = line + 10U;
	for (index = 0; index < 15; index++)
		if (!LearningToken(&cursor, fields[index], sizeof(fields[index]),
		        index == 14))
			return 0;
	if (!LearningU32(fields[0], &from) ||
	    !LearningI32(fields[1], &from_x) ||
	    !LearningI32(fields[2], &from_y) ||
	    !LearningI32(fields[3], &from_z) ||
	    !LearningU32(fields[4], &to) ||
	    !LearningI32(fields[5], &to_x) ||
	    !LearningI32(fields[6], &to_y) ||
	    !LearningI32(fields[7], &to_z) ||
	    !LearningU32(fields[8], &hint) ||
	    !LearningU32(fields[9], &has_waypoint) ||
	    !LearningI32(fields[10], &waypoint_x) ||
	    !LearningI32(fields[11], &waypoint_y) ||
	    !LearningI32(fields[12], &waypoint_z) ||
	    !LearningU64(fields[13], &first_sequence) ||
	    !LearningU64(fields[14], &last_sequence) ||
	    hint != SG_RUNE_LEARNING_DRY_RUN_WAYPOINT || has_waypoint > 1U ||
	    from_x < LEARNING_COORD_Q8_MIN || from_x > LEARNING_COORD_Q8_MAX ||
	    from_y < LEARNING_COORD_Q8_MIN || from_y > LEARNING_COORD_Q8_MAX ||
	    from_z < LEARNING_COORD_Q8_MIN || from_z > LEARNING_COORD_Q8_MAX ||
	    to_x < LEARNING_COORD_Q8_MIN || to_x > LEARNING_COORD_Q8_MAX ||
	    to_y < LEARNING_COORD_Q8_MIN || to_y > LEARNING_COORD_Q8_MAX ||
	    to_z < LEARNING_COORD_Q8_MIN || to_z > LEARNING_COORD_Q8_MAX ||
	    waypoint_x < LEARNING_COORD_Q8_MIN ||
	    waypoint_x > LEARNING_COORD_Q8_MAX ||
	    waypoint_y < LEARNING_COORD_Q8_MIN ||
	    waypoint_y > LEARNING_COORD_Q8_MAX ||
	    waypoint_z < LEARNING_COORD_Q8_MIN ||
	    waypoint_z > LEARNING_COORD_Q8_MAX ||
	    first_sequence == 0U || first_sequence > (uint64_t)INT64_MAX ||
	    last_sequence < first_sequence ||
	    last_sequence > (uint64_t)INT64_MAX ||
	    (!has_waypoint && (waypoint_x || waypoint_y || waypoint_z)))
		return 0;
	written = snprintf(canonical, sizeof(canonical),
		"candidate %u %d %d %d %u %d %d %d %u %u %d %d %d %llu %llu",
		(unsigned)from, (int)from_x, (int)from_y, (int)from_z,
		(unsigned)to, (int)to_x, (int)to_y, (int)to_z, (unsigned)hint,
		(unsigned)has_waypoint, (int)waypoint_x, (int)waypoint_y,
		(int)waypoint_z, (unsigned long long)first_sequence,
		(unsigned long long)last_sequence);
	if (written <= 0 || (size_t)written >= sizeof(canonical) ||
	    strcmp(line, canonical) != 0)
		return 0;
	memset(candidate, 0, sizeof(*candidate));
	candidate->source_from = from;
	candidate->source_to = to;
	candidate->from_origin_q8[0] = from_x;
	candidate->from_origin_q8[1] = from_y;
	candidate->from_origin_q8[2] = from_z;
	candidate->to_origin_q8[0] = to_x;
	candidate->to_origin_q8[1] = to_y;
	candidate->to_origin_q8[2] = to_z;
	candidate->waypoint_q8[0] = waypoint_x;
	candidate->waypoint_q8[1] = waypoint_y;
	candidate->waypoint_q8[2] = waypoint_z;
	candidate->first_sequence = (uint64_t)first_sequence;
	candidate->last_sequence = (uint64_t)last_sequence;
	candidate->has_waypoint = (uint8_t)has_waypoint;
	candidate->hint = (uint8_t)hint;
	return 1;
}

static int LearningCoordinatesMatch(const rune_t *source,
	const sg_rune_learning_candidate_t *candidate)
{
	int axis;

	if (candidate->source_from >= source->artifact.num_seeds ||
	    candidate->source_to >= source->artifact.num_seeds ||
	    candidate->source_from == candidate->source_to)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		float from = (float)candidate->from_origin_q8[axis] * 0.125f;
		float to = (float)candidate->to_origin_q8[axis] * 0.125f;

		if (!LearningSameFloat(
		        source->seeds[candidate->source_from].origin[axis], from) ||
		    !LearningSameFloat(
		        source->seeds[candidate->source_to].origin[axis], to))
			return 0;
	}
	return 1;
}

static int LearningCandidateCompare(
	const sg_rune_learning_candidate_t *left,
	const sg_rune_learning_candidate_t *right)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
		if (left->from_origin_q8[axis] != right->from_origin_q8[axis])
			return left->from_origin_q8[axis] < right->from_origin_q8[axis]
				? -1 : 1;
	for (axis = 0; axis < 3; axis++)
		if (left->to_origin_q8[axis] != right->to_origin_q8[axis])
			return left->to_origin_q8[axis] < right->to_origin_q8[axis]
				? -1 : 1;
	if (left->source_from != right->source_from)
		return left->source_from < right->source_from ? -1 : 1;
	if (left->source_to != right->source_to)
		return left->source_to < right->source_to ? -1 : 1;
	if (left->first_sequence != right->first_sequence)
		return left->first_sequence < right->first_sequence ? -1 : 1;
	for (axis = 0; axis < 3; axis++)
		if (left->waypoint_q8[axis] != right->waypoint_q8[axis])
			return left->waypoint_q8[axis] < right->waypoint_q8[axis]
				? -1 : 1;
	return 0;
}

static int LearningEndpointsEqual(
	const sg_rune_learning_candidate_t *left,
	const sg_rune_learning_candidate_t *right)
{
	return memcmp(left->from_origin_q8, right->from_origin_q8,
	           sizeof(left->from_origin_q8)) == 0 &&
	       memcmp(left->to_origin_q8, right->to_origin_q8,
	           sizeof(left->to_origin_q8)) == 0;
}

static int LearningHookCandidateLine(const char *line,
	sg_rune_learning_hook_candidate_t *candidate)
{
	char canonical[LEARNING_LINE_BYTES];
	char fields[19][32];
	const char *cursor;
	uint32_t from, to, rope_count;
	int32_t from_q8[3], to_q8[3], bite_q8[2][3];
	int16_t aim_short[2][2];
	int field = 0;
	int axis, rope, written;

	if (strncmp(line, "hook_candidate ", 15U) != 0)
		return 0;
	cursor = line + 15U;
	for (field = 0; field < 19; field++)
		if (!LearningToken(&cursor, fields[field], sizeof(fields[field]),
		        field == 18))
			return 0;
	if (!LearningU32(fields[0], &from))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (!LearningI32(fields[1 + axis], &from_q8[axis]))
			return 0;
	if (!LearningU32(fields[4], &to))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (!LearningI32(fields[5 + axis], &to_q8[axis]))
			return 0;
	if (!LearningU32(fields[8], &rope_count) ||
	    (rope_count != 1U && rope_count != 2U))
		return 0;
	for (rope = 0; rope < 2; rope++)
		for (axis = 0; axis < 2; axis++)
			if (!LearningI16(fields[9 + rope * 2 + axis],
			        &aim_short[rope][axis]))
				return 0;
	for (rope = 0; rope < 2; rope++)
		for (axis = 0; axis < 3; axis++)
			if (!LearningI32(fields[13 + rope * 3 + axis],
			        &bite_q8[rope][axis]))
				return 0;
	for (axis = 0; axis < 3; axis++)
		if (from_q8[axis] < LEARNING_COORD_Q8_MIN ||
		    from_q8[axis] > LEARNING_COORD_Q8_MAX ||
		    to_q8[axis] < LEARNING_COORD_Q8_MIN ||
		    to_q8[axis] > LEARNING_COORD_Q8_MAX)
			return 0;
	for (rope = 0; rope < 2; rope++)
		for (axis = 0; axis < 3; axis++)
			if (bite_q8[rope][axis] < LEARNING_COORD_Q8_MIN ||
			    bite_q8[rope][axis] > LEARNING_COORD_Q8_MAX)
				return 0;
	if (rope_count == 1U &&
	    (aim_short[1][0] != 0 || aim_short[1][1] != 0 ||
	     bite_q8[1][0] != 0 || bite_q8[1][1] != 0 ||
	     bite_q8[1][2] != 0))
		return 0;
	written = snprintf(canonical, sizeof(canonical),
		"hook_candidate %u %d %d %d %u %d %d %d %u %d %d %d %d "
		"%d %d %d %d %d %d",
		(unsigned)from, (int)from_q8[0], (int)from_q8[1],
		(int)from_q8[2], (unsigned)to, (int)to_q8[0],
		(int)to_q8[1], (int)to_q8[2], (unsigned)rope_count,
		(int)aim_short[0][0], (int)aim_short[0][1],
		(int)aim_short[1][0], (int)aim_short[1][1],
		(int)bite_q8[0][0], (int)bite_q8[0][1],
		(int)bite_q8[0][2], (int)bite_q8[1][0],
		(int)bite_q8[1][1], (int)bite_q8[1][2]);
	if (written <= 0 || (size_t)written >= sizeof(canonical) ||
	    strcmp(line, canonical) != 0)
		return 0;
	memset(candidate, 0, sizeof(*candidate));
	candidate->source_from = from;
	candidate->source_to = to;
	memcpy(candidate->from_origin_q8, from_q8, sizeof(from_q8));
	memcpy(candidate->to_origin_q8, to_q8, sizeof(to_q8));
	memcpy(candidate->aim_short, aim_short, sizeof(aim_short));
	memcpy(candidate->bite_q8, bite_q8, sizeof(bite_q8));
	candidate->rope_count = (uint8_t)rope_count;
	return 1;
}

static int LearningHookCoordinatesMatch(const rune_t *source,
	const sg_rune_learning_hook_candidate_t *candidate)
{
	sg_rune_learning_candidate_t run;

	memset(&run, 0, sizeof(run));
	run.source_from = candidate->source_from;
	run.source_to = candidate->source_to;
	memcpy(run.from_origin_q8, candidate->from_origin_q8,
		sizeof(run.from_origin_q8));
	memcpy(run.to_origin_q8, candidate->to_origin_q8,
		sizeof(run.to_origin_q8));
	return LearningCoordinatesMatch(source, &run);
}

static int LearningHookEndpointsCompare(
	const sg_rune_learning_hook_candidate_t *left,
	const sg_rune_learning_hook_candidate_t *right)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
		if (left->from_origin_q8[axis] != right->from_origin_q8[axis])
			return left->from_origin_q8[axis] < right->from_origin_q8[axis]
				? -1 : 1;
	for (axis = 0; axis < 3; axis++)
		if (left->to_origin_q8[axis] != right->to_origin_q8[axis])
			return left->to_origin_q8[axis] < right->to_origin_q8[axis]
				? -1 : 1;
	return 0;
}

static int LearningHookProofCompare(
	const sg_rune_learning_hook_candidate_t *left,
	const sg_rune_learning_hook_candidate_t *right)
{
	int rope, axis;
	int endpoints = LearningHookEndpointsCompare(left, right);

	if (endpoints != 0)
		return endpoints;
	if (left->rope_count != right->rope_count)
		return left->rope_count < right->rope_count ? -1 : 1;
	for (rope = 0; rope < 2; rope++)
		for (axis = 0; axis < 3; axis++)
			if (left->bite_q8[rope][axis] != right->bite_q8[rope][axis])
				return left->bite_q8[rope][axis] <
				        right->bite_q8[rope][axis] ? -1 : 1;
	return 0;
}

static int LearningHookCompare(
	const sg_rune_learning_hook_candidate_t *left,
	const sg_rune_learning_hook_candidate_t *right)
{
	int rope, axis;
	int proof = LearningHookProofCompare(left, right);

	if (proof != 0)
		return proof;
	for (rope = 0; rope < 2; rope++)
		for (axis = 0; axis < 2; axis++)
			if (left->aim_short[rope][axis] != right->aim_short[rope][axis])
				return left->aim_short[rope][axis] <
				        right->aim_short[rope][axis] ? -1 : 1;
	return 0;
}

sg_rune_learning_load_status_t SG_RuneLearningLoadFile(
	const rune_t *source, const char *path,
	const sg_rune_learning_storage_t *storage,
	sg_rune_learning_evidence_t *out)
{
	FILE *file = NULL;
	char line[LEARNING_LINE_BYTES];
	char trace_sha256[65];
	char replay_sha256[65];
	uint32_t format, count, hook_count = 0U;
	uint32_t index;

	if (out)
		memset(out, 0, sizeof(*out));
	if (!source || !path || !path[0] || !storage || !out ||
	    (storage->run_capacity != 0U && !storage->runs) ||
	    (storage->hook_capacity != 0U && !storage->hooks))
		return SG_RUNE_LEARNING_REJECTED;
	errno = 0;
	file = fopen(path, "rb");
	if (!file)
		return errno == ENOENT ? SG_RUNE_LEARNING_MISSING
			: SG_RUNE_LEARNING_REJECTED;
	if (!LearningArtifact(source, file, &format, &count, trace_sha256,
	        replay_sha256) || count > storage->run_capacity ||
	    (count != 0U && !storage->runs))
		goto reject;
	for (index = 0; index < count; index++)
	{
		if (LearningLine(file, line, sizeof(line)) != 1 ||
		    !LearningCandidateLine(line, &storage->runs[index]) ||
		    !LearningCoordinatesMatch(source, &storage->runs[index]) ||
		    (index != 0U &&
		     (LearningEndpointsEqual(&storage->runs[index - 1U],
		          &storage->runs[index]) ||
		      LearningCandidateCompare(&storage->runs[index - 1U],
		          &storage->runs[index]) >= 0)))
			goto reject;
	}
	if (format == LEARNING_FORMAT_TYPED)
	{
		char value[128];
		uint32_t pair_variants = 0U;

		if (!LearningField(file, "hook_candidates", value, sizeof(value)) ||
		    !LearningU32(value, &hook_count) ||
		    hook_count > SG_RUNE_LEARNING_MAX_HOOK_CANDIDATES ||
		    hook_count > storage->hook_capacity ||
		    (hook_count != 0U && !storage->hooks))
			goto reject;
		for (index = 0U; index < hook_count; index++)
		{
			sg_rune_learning_hook_candidate_t *candidate =
				&storage->hooks[index];

			if (LearningLine(file, line, sizeof(line)) != 1 ||
			    !LearningHookCandidateLine(line, candidate) ||
			    !LearningHookCoordinatesMatch(source, candidate))
				goto reject;
			if (index == 0U ||
			    LearningHookEndpointsCompare(&storage->hooks[index - 1U],
			        candidate) != 0)
				pair_variants = 1U;
			else
				pair_variants++;
			if (pair_variants > SG_RUNE_LEARNING_MAX_HOOKS_PER_PAIR ||
			    (index != 0U &&
			     (LearningHookProofCompare(&storage->hooks[index - 1U],
			          candidate) == 0 ||
			      LearningHookCompare(&storage->hooks[index - 1U],
			          candidate) >= 0)))
				goto reject;
		}
	}
	{
		int trailing = LearningLine(file, line, sizeof(line));
		int read_failed = ferror(file);
		int close_failed = fclose(file) != 0;

		file = NULL;
		if (trailing != -1 || read_failed || close_failed)
			goto reject;
	}
	strcpy(out->source_rune_sha256, source->encoded_sha256);
	strcpy(out->trace_sha256, trace_sha256);
	strcpy(out->replay_sha256, replay_sha256);
	out->candidate_count = count;
	out->candidates = storage->runs;
	out->hook_candidate_count = hook_count;
	out->hook_candidates = storage->hooks;
	return SG_RUNE_LEARNING_READY;

reject:
	if (file)
		fclose(file);
	memset(out, 0, sizeof(*out));
	return SG_RUNE_LEARNING_REJECTED;
}
