#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_configuration_audit.h"

/* Identity rows contain a map name, six 16-digit hexadecimal uint64 values,
 * twenty 8-digit hexadecimal binary32 bit patterns, and two 8-digit
 * hexadecimal frame values. No numeric field has a prefix or sign. */

static int HexDigit(unsigned char byte, uint8_t *value)
{
	if (byte >= (unsigned char)'0' && byte <= (unsigned char)'9')
		*value = (uint8_t)(byte - (unsigned char)'0');
	else if (byte >= (unsigned char)'a' && byte <= (unsigned char)'f')
		*value = (uint8_t)(byte - (unsigned char)'a' + 10U);
	else if (byte >= (unsigned char)'A' && byte <= (unsigned char)'F')
		*value = (uint8_t)(byte - (unsigned char)'A' + 10U);
	else
		return 0;
	return 1;
}

static int ParseHex(const char *text, uint32_t digits, uint64_t *value)
{
	uint64_t parsed = 0;
	uint32_t index;

	if (strlen(text) != digits)
		return 0;
	for (index = 0; index < digits; index++)
	{
		uint8_t digit;

		if (!HexDigit((unsigned char)text[index], &digit))
			return 0;
		parsed = (parsed << 4U) | digit;
	}
	*value = parsed;
	return 1;
}

static int ParseHex32(const char *text, uint32_t *value)
{
	uint64_t parsed;

	if (!ParseHex(text, 8U, &parsed))
		return 0;
	*value = (uint32_t)parsed;
	return 1;
}

static int ParseFloatBits(const char *text, float *value)
{
	uint32_t bits;

	if (!ParseHex32(text, &bits))
		return 0;
	memcpy(value, &bits, sizeof(bits));
	return 1;
}

static int ReadIdentity(FILE *file, const char *expected_name,
	sg_rune_model_identity_t *identity)
{
	char line[4096];
	char *tokens[29];
	char *token;
	uint32_t count = 0, index, float_index = 0;
	uint64_t *ids[6];
	float *floats[20];

	if (!fgets(line, sizeof(line), file))
		return 0;
	for (token = strtok(line, " \t\r\n"); token && count < 29U;
		token = strtok(NULL, " \t\r\n"))
		tokens[count++] = token;
	if (count != 29U || token || strcmp(tokens[0], expected_name))
		return 0;
	memset(identity, 0, sizeof(*identity));
	ids[0] = &identity->bsp_content_id;
	ids[1] = &identity->entity_semantics_id;
	ids[2] = &identity->physics_abi_id;
	ids[3] = &identity->source_set_identity;
	ids[4] = &identity->schema_id;
	ids[5] = &identity->producer_identity;
	for (index = 0; index < 6U; index++)
		if (!ParseHex(tokens[index + 1U], 16U, ids[index]))
			return 0;
#define ADD_VECTOR(vector) do { \
	floats[float_index++] = &(vector).value[0]; \
	floats[float_index++] = &(vector).value[1]; \
	floats[float_index++] = &(vector).value[2]; } while (0)
	ADD_VECTOR(identity->standing_hull.mins);
	ADD_VECTOR(identity->standing_hull.maxs);
	ADD_VECTOR(identity->crouching_hull.mins);
	ADD_VECTOR(identity->crouching_hull.maxs);
#undef ADD_VECTOR
	floats[float_index++] = &identity->physics.gravity;
	floats[float_index++] = &identity->physics.ground_acceleration;
	floats[float_index++] = &identity->physics.air_acceleration;
	floats[float_index++] = &identity->physics.water_acceleration;
	floats[float_index++] = &identity->physics.hook_acceleration;
	floats[float_index++] = &identity->physics.external_acceleration;
	floats[float_index++] = &identity->physics.water_drag;
	floats[float_index++] = &identity->physics.max_velocity;
	for (index = 0; index < 20U; index++)
		if (!ParseFloatBits(tokens[index + 7U], floats[index]))
			return 0;
	return ParseHex32(tokens[27], &identity->physics.frame_ms) &&
		ParseHex32(tokens[28], &identity->physics.substep_ms);
}

static int RunMap(const char *asset_dir, const char *name,
	const sg_rune_model_identity_t *identity)
{
	char path[4096];
	int path_length;
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t bsp_error;
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t host_error;
	sg_configuration_space_t *space = NULL;
	sg_configuration_error_t build_error;
	sg_configuration_audit_result_t audit;
	int result = 0;

	path_length = snprintf(path, sizeof(path), "%s/%s.bsp", asset_dir, name);
	if (path_length < 0 || (size_t)path_length >= sizeof(path))
		return 0;
	if (!SG_BspWorldLoadFile(path, &world, &bsp_error))
	{
		fprintf(stderr, "%s: BSP load: %s lump=%u record=%u\n", name,
			SG_BspWorldErrorString(bsp_error.code), (unsigned)bsp_error.lump,
			bsp_error.record);
		goto done;
	}
	if (!SG_HostCollisionInit(&authority, world, identity, &host_error))
	{
		fprintf(stderr, "%s: host init: %s\n", name,
			SG_HostCollisionErrorString(host_error));
		goto done;
	}
	if (!SG_ConfigurationBuild(&authority, NULL, &space, &build_error))
	{
		fprintf(stderr, "%s: construction: %s source=%u\n", name,
			SG_ConfigurationErrorString(build_error.code),
			build_error.source_index);
		goto done;
	}
	if (!SG_ConfigurationAudit(&authority, space, &audit))
	{
		fprintf(stderr, "%s: audit: %s record=%u kind=%u cells=%u portals=%u\n", name,
			SG_ConfigurationAuditCodeString(audit.code), audit.record,
			audit.record < space->certificate_node_count ?
				(unsigned)space->certificate_nodes[audit.record].kind : UINT32_MAX,
			space->cell_count, space->portal_count);
		goto done;
	}
	fprintf(stdout, "%s cells=%u portals=%u overlaps=%u witnesses=%llu\n", name,
		space->cell_count, space->portal_count, space->stance_overlap_count,
		(unsigned long long)audit.boundary_witnesses);
	result = 1;

done:
	SG_ConfigurationDestroy(space);
	SG_BspWorldDestroy(world);
	return result;
}

int main(int argc, char **argv)
{
	FILE *manifest;
	FILE *identities;
	char name[256];
	uint64_t ordinal = 0;
	int failed = 0;
	int identity_read_failed = 0;

	if (argc != 4)
	{
		fprintf(stderr, "usage: %s MANIFEST ASSET_DIR AUTHORITATIVE_IDENTITY_TSV\n",
			argv[0]);
		return 2;
	}
	manifest = fopen(argv[1], "r");
	if (!manifest)
	{
		perror(argv[1]);
		return 2;
	}
	identities = fopen(argv[3], "r");
	if (!identities)
	{
		perror(argv[3]);
		fclose(manifest);
		return 2;
	}
	while (fgets(name, sizeof(name), manifest))
	{
		size_t length = strcspn(name, "\r\n");

		name[length] = '\0';
		if (!length)
			continue;
		{
			sg_rune_model_identity_t identity;

			if (!ReadIdentity(identities, name, &identity))
			{
				fprintf(stderr, "%s: missing or malformed authoritative identity\n",
					name);
				failed++;
				identity_read_failed = 1;
				break;
			}
			if (!RunMap(argv[2], name, &identity))
				failed++;
		}
		ordinal++;
	}
	if (ferror(manifest))
		failed++;
	fclose(manifest);
	if (!identity_read_failed)
	{
		char extra[2];

		if (fgets(extra, sizeof(extra), identities) || ferror(identities))
			failed++;
	}
	fclose(identities);
	if (ordinal != UINT64_C(175))
	{
		fprintf(stderr, "manifest count %llu, expected 175\n",
			(unsigned long long)ordinal);
		failed++;
	}
	if (failed)
	{
		fprintf(stderr, "%d of %llu manifest BSP checks failed\n", failed,
			(unsigned long long)ordinal);
		return 1;
	}
	return 0;
}
