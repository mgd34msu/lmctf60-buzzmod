#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif

#include "../slipgate/sg_rune_compact_wire.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <sys/types.h>
typedef off_t runecompactread_file_offset_t;
#else
typedef __int64 runecompactread_file_offset_t;
#endif

static int runecompactread_file_seek(FILE *stream,
	runecompactread_file_offset_t offset, int origin)
{
#ifndef _WIN32
	return fseeko(stream, offset, origin);
#else
	return _fseeki64(stream, offset, origin);
#endif
}

static runecompactread_file_offset_t runecompactread_file_tell(FILE *stream)
{
#ifndef _WIN32
	return ftello(stream);
#else
	return _ftelli64(stream);
#endif
}

static int runecompactread_file(const char *path, unsigned char **image_out,
	size_t *image_size_out)
{
	FILE *stream;
	runecompactread_file_offset_t length;
	uintmax_t unsigned_length;
	unsigned char *image;
	size_t image_size;
	int read_ok;

	stream = fopen(path, "rb");
	if (stream == NULL)
		return 0;
	if (runecompactread_file_seek(stream, (runecompactread_file_offset_t)0,
		SEEK_END) != 0 ||
		(length = runecompactread_file_tell(stream)) <
			(runecompactread_file_offset_t)0)
	{
		(void)fclose(stream);
		return 0;
	}
	unsigned_length = (uintmax_t)length;
	if (unsigned_length > (uintmax_t)SIZE_MAX ||
		unsigned_length > (uintmax_t)SG_RuneCompactWireImageLimit() ||
		runecompactread_file_seek(stream, (runecompactread_file_offset_t)0,
			SEEK_SET) != 0)
	{
		(void)fclose(stream);
		return 0;
	}
	image_size = (size_t)unsigned_length;
	image = malloc(image_size == 0U ? 1U : image_size);
	if (image == NULL)
	{
		(void)fclose(stream);
		return 0;
	}
	read_ok = image_size == 0U ||
		fread(image, 1U, image_size, stream) == image_size;
	if (fclose(stream) != 0)
		read_ok = 0;
	if (!read_ok)
	{
		free(image);
		return 0;
	}
	*image_out = image;
	*image_size_out = image_size;
	return 1;
}

static void runecompactread_print_hex(const uint8_t *bytes, size_t count)
{
	size_t index;

	for (index = 0U; index < count; ++index)
		(void)printf("%02" PRIx8, bytes[index]);
}

static void runecompactread_print_vec3(const sg_rune_q8_vec3_t *vector)
{
	(void)printf("[%" PRIi32 ",%" PRIi32 ",%" PRIi32 "]",
		vector->value[0], vector->value[1], vector->value[2]);
}

static void runecompactread_print_summary(
	const sg_rune_compact_wire_info_t *info)
{
	const sg_rune_compact_identity_t *identity = &info->identity;
	uint32_t section;

	(void)printf("{\"identity\":{\"bsp_sha256\":\"");
	runecompactread_print_hex(identity->bsp_sha256,
		sizeof(identity->bsp_sha256));
	(void)printf(
		"\",\"bsp_bytes\":%" PRIu64
		",\"bsp_checksum\":%" PRIu32
		",\"entity_crc32\":%" PRIu32
		",\"entity_semantics_id\":%" PRIu64
		",\"physics_abi_id\":%" PRIu64
		",\"collision_law_id\":%" PRIu64
		",\"pmove_law_id\":%" PRIu64
		",\"gravity_law_id\":%" PRIu64
		",\"hook_law_id\":%" PRIu64
		",\"mechanism_law_id\":%" PRIu64
		",\"weapon_law_id\":%" PRIu64
		",\"construction_id\":%" PRIu64
		",\"schema_id\":%" PRIu64
		",\"producer_identity\":%" PRIu64
		",\"weapon_profile_catalog_id\":%" PRIu64,
		identity->bsp_bytes, identity->bsp_checksum,
		identity->entity_crc32, identity->entity_semantics_id,
		identity->physics_abi_id, identity->collision_law_id,
		identity->pmove_law_id, identity->gravity_law_id,
		identity->hook_law_id, identity->mechanism_law_id,
		identity->weapon_law_id, identity->construction_id,
		identity->schema_id, identity->producer_identity,
		identity->weapon_profile_catalog_id);
	(void)printf(
		",\"source_counts\":{\"model_count\":%" PRIu32
		",\"leaf_count\":%" PRIu32
		",\"area_count\":%" PRIu32
		",\"plane_count\":%" PRIu32
		",\"brush_count\":%" PRIu32
		",\"brush_side_count\":%" PRIu32
		",\"entity_count\":%" PRIu32 "}",
		identity->source_counts.model_count,
		identity->source_counts.leaf_count,
		identity->source_counts.area_count,
		identity->source_counts.plane_count,
		identity->source_counts.brush_count,
		identity->source_counts.brush_side_count,
		identity->source_counts.entity_count);
	(void)printf(",\"standing_hull\":{\"mins\":");
	runecompactread_print_vec3(&identity->standing_hull.mins);
	(void)printf(",\"maxs\":");
	runecompactread_print_vec3(&identity->standing_hull.maxs);
	(void)printf("},\"crouching_hull\":{\"mins\":");
	runecompactread_print_vec3(&identity->crouching_hull.mins);
	(void)printf(",\"maxs\":");
	runecompactread_print_vec3(&identity->crouching_hull.maxs);
	(void)printf(
		"},\"physics\":{\"gravity_bits\":%" PRIu32
		",\"ground_acceleration_bits\":%" PRIu32
		",\"air_acceleration_bits\":%" PRIu32
		",\"water_acceleration_bits\":%" PRIu32
		",\"hook_acceleration_bits\":%" PRIu32
		",\"external_acceleration_bits\":%" PRIu32
		",\"water_drag_bits\":%" PRIu32
		",\"max_velocity_bits\":%" PRIu32
		",\"frame_ms\":%" PRIu32
		",\"substep_ms\":%" PRIu32 "}},\"counts\":{",
		identity->physics.gravity_bits,
		identity->physics.ground_acceleration_bits,
		identity->physics.air_acceleration_bits,
		identity->physics.water_acceleration_bits,
		identity->physics.hook_acceleration_bits,
		identity->physics.external_acceleration_bits,
		identity->physics.water_drag_bits,
		identity->physics.max_velocity_bits,
		identity->physics.frame_ms, identity->physics.substep_ms);
	for (section = 0U;
		section < (uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT; ++section)
	{
		if (section != 0U)
			(void)putchar(',');
		(void)printf("\"%s\":%" PRIu32,
			SG_RuneCompactWireSectionName(
				(sg_rune_compact_wire_section_t)section),
			info->counts[section]);
	}
	(void)printf("}}\n");
}

int main(int argc, char **argv)
{
	unsigned char *image = NULL;
	size_t image_size = 0U;
	sg_rune_compact_wire_info_t info;
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_decoded_t *decoded = NULL;

	if (argc != 2)
	{
		(void)fprintf(stderr, "usage: runecompactread FILE\n");
		return 2;
	}
	if (!runecompactread_file(argv[1], &image, &image_size))
	{
		(void)fprintf(stderr, "runecompactread: cannot read %s\n", argv[1]);
		return 2;
	}
	if (!SG_RuneCompactWireInspect(image, image_size, &info, &error) ||
		!SG_RuneCompactWireDecode(image, image_size, &info.identity, &decoded,
			&error))
	{
		(void)fprintf(stderr,
			"runecompactread: reject: %s section=%s record=%" PRIu32 "\n",
			SG_RuneCompactWireErrorString(error.code),
			SG_RuneCompactWireSectionName(error.section), error.record);
		free(image);
		return 1;
	}
	runecompactread_print_summary(&info);
	SG_RuneCompactWireDestroy(decoded);
	free(image);
	if (ferror(stdout) || fflush(stdout) != 0)
	{
		(void)fprintf(stderr, "runecompactread: cannot write summary\n");
		return 2;
	}
	return 0;
}
