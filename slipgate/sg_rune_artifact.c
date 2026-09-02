#define _POSIX_C_SOURCE 200809L
#include "sg_rune_artifact.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "sg_crc32.h"

#define ALIGNMENT 16U

static const uint8_t MAGIC[8] = { 'R', 'U', 'N', 'E', '4', 0, 0, 0 };

typedef struct header_s
{
	uint8_t magic[8];
	uint32_t version;
	uint32_t section_count;
	uint64_t image_bytes;
	uint32_t payload_crc32;   /* over everything after this header */
	uint32_t reserved;
	sg_rune_identity_t identity;
	sg_rune_law_t law;
} header_t;

typedef struct section_s
{
	uint32_t kind;
	uint32_t element_size;
	uint32_t count;
	uint32_t reserved;
	uint64_t offset;
	uint64_t bytes;
} section_t;

typedef struct layout_s
{
	const void *data;
	uint32_t count;
	size_t element_size;
} layout_t;

static void Layouts(const sg_rune_artifact_t *source,
	layout_t layouts[SG_RUNE_SECTION_COUNT])
{
	const sg_rune_cx_view_t *cx = &source->complex;
	const sg_rune_move_table_t *move = &source->movement;
#define LAYOUT(kind_, array_, count_) \
	do { layouts[kind_].data = (array_); layouts[kind_].count = (count_); \
		layouts[kind_].element_size = sizeof(*(array_)); } while (0)
	LAYOUT(SG_RUNE_SECTION_CELLS, cx->cells, cx->cell_count);
	LAYOUT(SG_RUNE_SECTION_FACETS, cx->facets, cx->facet_count);
	LAYOUT(SG_RUNE_SECTION_INCIDENCES, cx->incidences, cx->incidence_count);
	LAYOUT(SG_RUNE_SECTION_CELL_INCIDENCES, cx->cell_incidences,
		cx->cell_incidence_count);
	LAYOUT(SG_RUNE_SECTION_VERTICES, cx->vertices, cx->vertex_count);
	LAYOUT(SG_RUNE_SECTION_PORTALS, cx->portals, cx->portal_count);
	LAYOUT(SG_RUNE_SECTION_SURFACES, cx->surfaces, cx->surface_count);
	LAYOUT(SG_RUNE_SECTION_SURFACE_VERTICES, cx->surface_vertices,
		cx->surface_vertex_count);
	LAYOUT(SG_RUNE_SECTION_MOVE_CAPABILITIES, move->capabilities,
		move->capability_count);
	LAYOUT(SG_RUNE_SECTION_MOVE_PROFILES, move->profiles, move->profile_count);
	LAYOUT(SG_RUNE_SECTION_FN_FUNCTIONS, move->analytic.functions,
		move->analytic.function_count);
	LAYOUT(SG_RUNE_SECTION_FN_TERMS, move->analytic.terms,
		move->analytic.term_count);
	LAYOUT(SG_RUNE_SECTION_FN_CLAUSES, move->analytic.clauses,
		move->analytic.clause_count);
#undef LAYOUT
}

static size_t AlignUp(size_t value)
{
	return (value + ALIGNMENT - 1U) & ~((size_t)ALIGNMENT - 1U);
}

sg_rune_artifact_status_t SG_RuneArtifactEncode(const sg_rune_artifact_t *source,
	unsigned char **image_out, size_t *image_size_out)
{
	layout_t layouts[SG_RUNE_SECTION_COUNT];
	section_t sections[SG_RUNE_SECTION_COUNT];
	header_t header;
	size_t offset, total;
	unsigned char *image;
	uint32_t crc;
	uint32_t index;

	if (image_out)
		*image_out = NULL;
	if (image_size_out)
		*image_size_out = 0U;
	if (!source || !image_out || !image_size_out)
		return SG_RUNE_ARTIFACT_INVALID_ARGUMENT;
	Layouts(source, layouts);
	offset = AlignUp(sizeof(header) + sizeof(sections));
	memset(sections, 0, sizeof(sections));
	for (index = 0U; index < SG_RUNE_SECTION_COUNT; index++)
	{
		size_t bytes = (size_t)layouts[index].count * layouts[index].element_size;

		if (layouts[index].count && !layouts[index].data)
			return SG_RUNE_ARTIFACT_INVALID_ARGUMENT;
		sections[index].kind = index;
		sections[index].element_size = (uint32_t)layouts[index].element_size;
		sections[index].count = layouts[index].count;
		sections[index].offset = offset;
		sections[index].bytes = bytes;
		offset = AlignUp(offset + bytes);
	}
	total = offset;
	image = calloc(1U, total ? total : 1U);
	if (!image)
		return SG_RUNE_ARTIFACT_OUT_OF_MEMORY;
	for (index = 0U; index < SG_RUNE_SECTION_COUNT; index++)
		if (sections[index].bytes)
			memcpy(image + sections[index].offset, layouts[index].data,
				(size_t)sections[index].bytes);
	memcpy(image + sizeof(header), sections, sizeof(sections));
	if (!SG_CRC32Buffer(image + sizeof(header), total - sizeof(header), &crc))
	{
		free(image);
		return SG_RUNE_ARTIFACT_INVALID_ARGUMENT;
	}
	memset(&header, 0, sizeof(header));
	memcpy(header.magic, MAGIC, sizeof(MAGIC));
	header.version = SG_RUNE_ARTIFACT_VERSION;
	header.section_count = SG_RUNE_SECTION_COUNT;
	header.image_bytes = total;
	header.payload_crc32 = crc;
	header.identity = source->identity;
	header.law = source->law;
	memcpy(image, &header, sizeof(header));
	*image_out = image;
	*image_size_out = total;
	return SG_RUNE_ARTIFACT_OK;
}

static const size_t ELEMENT_SIZES[SG_RUNE_SECTION_COUNT] = {
	sizeof(sg_rune_cx_cell_t), sizeof(sg_rune_cx_facet_t),
	sizeof(sg_rune_cx_incidence_t), sizeof(uint32_t),
	sizeof(sg_rune_cx_vec3_t), sizeof(sg_rune_cx_portal_t),
	sizeof(sg_rune_cx_surface_t), sizeof(sg_rune_cx_vec3_t),
	sizeof(sg_rune_move_capability_t), sizeof(sg_rune_move_profile_t),
	sizeof(sg_rune_fn_function_t), sizeof(sg_rune_fn_term_t),
	sizeof(sg_rune_fn_clause_t)
};

sg_rune_artifact_status_t SG_RuneArtifactDecode(const unsigned char *image,
	size_t image_size, sg_rune_artifact_t *artifact_out,
	sg_rune_fault_t *fault_out)
{
	header_t header;
	section_t sections[SG_RUNE_SECTION_COUNT];
	const void *arrays[SG_RUNE_SECTION_COUNT];
	uint32_t counts[SG_RUNE_SECTION_COUNT];
	uint32_t crc;
	uint32_t index;
	sg_rune_cx_view_t *cx;
	sg_rune_move_table_t *move;

	if (fault_out)
		memset(fault_out, 0, sizeof(*fault_out));
	if (!artifact_out)
		return SG_RUNE_ARTIFACT_INVALID_ARGUMENT;
	memset(artifact_out, 0, sizeof(*artifact_out));
	if (!image || ((uintptr_t)image % ALIGNMENT) != 0U)
		return SG_RUNE_ARTIFACT_INVALID_ARGUMENT;
	if (image_size < sizeof(header) + sizeof(sections))
		return SG_RUNE_ARTIFACT_TRUNCATED;
	memcpy(&header, image, sizeof(header));
	if (memcmp(header.magic, MAGIC, sizeof(MAGIC)) != 0)
		return SG_RUNE_ARTIFACT_BAD_MAGIC;
	if (header.version != SG_RUNE_ARTIFACT_VERSION ||
		header.section_count != SG_RUNE_SECTION_COUNT ||
		header.reserved != 0U)
		return SG_RUNE_ARTIFACT_BAD_VERSION;
	if (header.image_bytes != image_size)
		return SG_RUNE_ARTIFACT_TRUNCATED;
	if (!SG_CRC32Buffer(image + sizeof(header), image_size - sizeof(header),
		&crc) || crc != header.payload_crc32)
		return SG_RUNE_ARTIFACT_BAD_CHECKSUM;
	memcpy(sections, image + sizeof(header), sizeof(sections));
	for (index = 0U; index < SG_RUNE_SECTION_COUNT; index++)
	{
		const section_t *section = &sections[index];

		if (section->kind != index || section->reserved != 0U ||
			section->element_size != ELEMENT_SIZES[index] ||
			section->bytes != (uint64_t)section->count * section->element_size ||
			section->offset > image_size ||
			section->bytes > image_size - section->offset ||
			(section->offset % ALIGNMENT) != 0U)
			return SG_RUNE_ARTIFACT_BAD_SECTION;
		arrays[index] = section->count ? image + section->offset : NULL;
		counts[index] = section->count;
	}
	artifact_out->identity = header.identity;
	artifact_out->law = header.law;
	cx = &artifact_out->complex;
	move = &artifact_out->movement;
	cx->cells = arrays[SG_RUNE_SECTION_CELLS];
	cx->cell_count = counts[SG_RUNE_SECTION_CELLS];
	cx->facets = arrays[SG_RUNE_SECTION_FACETS];
	cx->facet_count = counts[SG_RUNE_SECTION_FACETS];
	cx->incidences = arrays[SG_RUNE_SECTION_INCIDENCES];
	cx->incidence_count = counts[SG_RUNE_SECTION_INCIDENCES];
	cx->cell_incidences = arrays[SG_RUNE_SECTION_CELL_INCIDENCES];
	cx->cell_incidence_count = counts[SG_RUNE_SECTION_CELL_INCIDENCES];
	cx->vertices = arrays[SG_RUNE_SECTION_VERTICES];
	cx->vertex_count = counts[SG_RUNE_SECTION_VERTICES];
	cx->portals = arrays[SG_RUNE_SECTION_PORTALS];
	cx->portal_count = counts[SG_RUNE_SECTION_PORTALS];
	cx->surfaces = arrays[SG_RUNE_SECTION_SURFACES];
	cx->surface_count = counts[SG_RUNE_SECTION_SURFACES];
	cx->surface_vertices = arrays[SG_RUNE_SECTION_SURFACE_VERTICES];
	cx->surface_vertex_count = counts[SG_RUNE_SECTION_SURFACE_VERTICES];
	move->capabilities = arrays[SG_RUNE_SECTION_MOVE_CAPABILITIES];
	move->capability_count = counts[SG_RUNE_SECTION_MOVE_CAPABILITIES];
	move->profiles = arrays[SG_RUNE_SECTION_MOVE_PROFILES];
	move->profile_count = counts[SG_RUNE_SECTION_MOVE_PROFILES];
	move->analytic.functions = arrays[SG_RUNE_SECTION_FN_FUNCTIONS];
	move->analytic.function_count = counts[SG_RUNE_SECTION_FN_FUNCTIONS];
	move->analytic.terms = arrays[SG_RUNE_SECTION_FN_TERMS];
	move->analytic.term_count = counts[SG_RUNE_SECTION_FN_TERMS];
	move->analytic.clauses = arrays[SG_RUNE_SECTION_FN_CLAUSES];
	move->analytic.clause_count = counts[SG_RUNE_SECTION_FN_CLAUSES];
	artifact_out->image = image;
	artifact_out->image_size = image_size;
	if (!SG_RuneArtifactValid(artifact_out, fault_out))
	{
		memset(artifact_out, 0, sizeof(*artifact_out));
		return SG_RUNE_ARTIFACT_BAD_RECORDS;
	}
	return SG_RUNE_ARTIFACT_OK;
}

/* ---- validation ----------------------------------------------------------- */

static int FunctionSlot(const sg_rune_fn_table_t *table, uint32_t function,
	sg_rune_fn_output_t output)
{
	if (function == SG_RUNE_FN_INDEX_NONE)
		return 1;
	return function < table->function_count &&
		table->functions[function].output == output;
}

static int Finite(float value)
{
	return isfinite(value) != 0;
}

static int LawValid(const sg_rune_law_t *law)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (!Finite(law->standing_mins[axis]) ||
			!Finite(law->standing_maxs[axis]) ||
			!Finite(law->crouching_mins[axis]) ||
			!Finite(law->crouching_maxs[axis]) ||
			!(law->standing_mins[axis] < law->standing_maxs[axis]) ||
			!(law->crouching_mins[axis] < law->crouching_maxs[axis]))
			return 0;
	return Finite(law->gravity) && law->gravity > 0.0f &&
		Finite(law->ground_acceleration) && Finite(law->air_acceleration) &&
		Finite(law->water_acceleration) && Finite(law->hook_acceleration) &&
		Finite(law->water_drag) && Finite(law->max_velocity) &&
		law->frame_ms > 0U && law->substep_ms > 0U &&
		law->substep_ms <= law->frame_ms &&
		law->reserved[0] == 0U && law->reserved[1] == 0U &&
		law->reserved[2] == 0U;
}

static int Fault(sg_rune_fault_t *fault_out, const char *array,
	uint32_t record, const char *reason)
{
	if (fault_out)
	{
		fault_out->array = array;
		fault_out->record = record;
		fault_out->reason = reason;
	}
	return 0;
}

int SG_RuneArtifactValid(const sg_rune_artifact_t *artifact,
	sg_rune_fault_t *fault_out)
{
	const sg_rune_cx_view_t *cx;
	const sg_rune_move_table_t *move;
	uint32_t index;

	if (fault_out)
		memset(fault_out, 0, sizeof(*fault_out));
	if (!artifact)
		return Fault(fault_out, "artifact", 0U, "missing");
	if (!LawValid(&artifact->law))
		return Fault(fault_out, "law", 0U, "value");
	if (artifact->identity.schema_id != SG_RUNE_ARTIFACT_SCHEMA_ID)
		return Fault(fault_out, "identity", 0U, "schema");
	if (!SG_RuneCxViewValid(&artifact->complex, fault_out))
		return 0;
	if (!SG_RuneFnTableValid(&artifact->movement.analytic))
		return Fault(fault_out, "functions", 0U, "table");
	cx = &artifact->complex;
	move = &artifact->movement;
	if ((move->capability_count && !move->capabilities) ||
		(move->profile_count && !move->profiles))
		return 0;
	for (index = 0U; index < move->profile_count; index++)
	{
		const sg_rune_move_profile_t *profile = &move->profiles[index];

		if (!FunctionSlot(&move->analytic, profile->cost,
				SG_RUNE_FN_OUTPUT_COST) ||
			!FunctionSlot(&move->analytic, profile->travel_time,
				SG_RUNE_FN_OUTPUT_TRAVEL_TIME_SECONDS) ||
			!FunctionSlot(&move->analytic, profile->position[0],
				SG_RUNE_FN_OUTPUT_POSITION_X) ||
			!FunctionSlot(&move->analytic, profile->position[1],
				SG_RUNE_FN_OUTPUT_POSITION_Y) ||
			!FunctionSlot(&move->analytic, profile->position[2],
				SG_RUNE_FN_OUTPUT_POSITION_Z) ||
			!FunctionSlot(&move->analytic, profile->velocity[0],
				SG_RUNE_FN_OUTPUT_VELOCITY_X) ||
			!FunctionSlot(&move->analytic, profile->velocity[1],
				SG_RUNE_FN_OUTPUT_VELOCITY_Y) ||
			!FunctionSlot(&move->analytic, profile->velocity[2],
				SG_RUNE_FN_OUTPUT_VELOCITY_Z) ||
			!FunctionSlot(&move->analytic, profile->reachability,
				SG_RUNE_FN_OUTPUT_REACHABILITY) ||
			!Finite(profile->lead_seconds) || profile->lead_seconds < 0.0f)
			return Fault(fault_out, "profiles", index, "function");
	}
	for (index = 0U; index < move->capability_count; index++)
	{
		const sg_rune_move_capability_t *capability = &move->capabilities[index];
		const sg_rune_cx_portal_t *portal;

		if (capability->cell >= cx->cell_count ||
			(capability->portal >= cx->portal_count &&
				capability->portal != SG_RUNE_CX_INDEX_NONE) ||
			(capability->portal == SG_RUNE_CX_INDEX_NONE &&
				capability->mechanism == SG_RUNE_CX_INDEX_NONE) ||
			capability->destination >= cx->cell_count ||
			capability->destination == capability->cell ||
			!Finite(capability->launch_velocity[0]) ||
			!Finite(capability->launch_velocity[1]) ||
			!Finite(capability->launch_velocity[2]) ||
			!Finite(capability->seconds) || capability->seconds < 0.0f ||
			capability->kind >= SG_RUNE_MOVE_KIND_COUNT ||
			capability->profile >= move->profile_count ||
			capability->reserved != 0U ||
			capability->source_stances == 0U ||
			capability->destination_stances == 0U ||
			(capability->source_stances &
				~(SG_RUNE_MOVE_STANDING | SG_RUNE_MOVE_CROUCHING)) != 0U ||
			(capability->destination_stances &
				~(SG_RUNE_MOVE_STANDING | SG_RUNE_MOVE_CROUCHING)) != 0U)
			return Fault(fault_out, "capabilities", index, "field");
		if (capability->portal == SG_RUNE_CX_INDEX_NONE)
			continue;
		portal = &cx->portals[capability->portal];
		if (cx->incidences[portal->source_incidence].cell != capability->cell &&
			cx->incidences[portal->destination_incidence].cell != capability->cell)
			return Fault(fault_out, "capabilities", index, "cell not on portal");
	}
	return 1;
}

int SG_RuneIdentityMatches(const sg_rune_identity_t *a,
	const sg_rune_identity_t *b)
{
	return a && b && memcmp(a, b, sizeof(*a)) == 0;
}

int SG_RuneLawMatches(const sg_rune_law_t *a, const sg_rune_law_t *b)
{
	return a && b && memcmp(a, b, sizeof(*a)) == 0;
}

/* ---- files ---------------------------------------------------------------- */

void SG_RuneArtifactRelease(sg_rune_artifact_t *artifact)
{
	if (!artifact)
		return;
	free(artifact->owned);
	memset(artifact, 0, sizeof(*artifact));
}

sg_rune_artifact_status_t SG_RuneArtifactLoadFile(const char *path,
	sg_rune_artifact_t *artifact_out, int *os_error_out,
	sg_rune_fault_t *fault_out)
{
	if (fault_out)
		memset(fault_out, 0, sizeof(*fault_out));
	FILE *file;
	long length;
	unsigned char *image;
	size_t size;
	sg_rune_artifact_status_t status;

	if (os_error_out)
		*os_error_out = 0;
	if (!artifact_out)
		return SG_RUNE_ARTIFACT_INVALID_ARGUMENT;
	memset(artifact_out, 0, sizeof(*artifact_out));
	if (!path)
		return SG_RUNE_ARTIFACT_INVALID_ARGUMENT;
	file = fopen(path, "rb");
	if (!file || fseek(file, 0L, SEEK_END) != 0 ||
		(length = ftell(file)) < 0L || fseek(file, 0L, SEEK_SET) != 0)
	{
		if (os_error_out)
			*os_error_out = errno;
		if (file)
			fclose(file);
		return SG_RUNE_ARTIFACT_FILE_ERROR;
	}
	size = (size_t)length;
	image = malloc(size ? size : 1U);
	if (!image)
	{
		fclose(file);
		return SG_RUNE_ARTIFACT_OUT_OF_MEMORY;
	}
	if (fread(image, 1U, size, file) != size)
	{
		if (os_error_out)
			*os_error_out = errno;
		fclose(file);
		free(image);
		return SG_RUNE_ARTIFACT_FILE_ERROR;
	}
	fclose(file);
	status = SG_RuneArtifactDecode(image, size, artifact_out, fault_out);
	if (status != SG_RUNE_ARTIFACT_OK)
	{
		free(image);
		return status;
	}
	artifact_out->owned = image;
	return SG_RUNE_ARTIFACT_OK;
}

sg_rune_artifact_status_t SG_RuneArtifactWriteFile(const char *path,
	const unsigned char *image, size_t image_size, int *os_error_out)
{
	char temporary[1024];
	FILE *file;
	int written;
	int failed = 0;

	if (os_error_out)
		*os_error_out = 0;
	if (!path || (!image && image_size))
		return SG_RUNE_ARTIFACT_INVALID_ARGUMENT;
	written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	if (written < 0 || (size_t)written >= sizeof(temporary))
		return SG_RUNE_ARTIFACT_INVALID_ARGUMENT;
	file = fopen(temporary, "wb");
	if (!file)
	{
		if (os_error_out)
			*os_error_out = errno;
		return SG_RUNE_ARTIFACT_FILE_ERROR;
	}
	if (fwrite(image, 1U, image_size, file) != image_size || fflush(file) != 0)
		failed = 1;
#ifndef _WIN32
	if (!failed && fsync(fileno(file)) != 0)
		failed = 1;
#endif
	if (fclose(file) != 0)
		failed = 1;
#ifdef _WIN32
	if (!failed)
		remove(path);
#endif
	if (!failed && rename(temporary, path) != 0)
		failed = 1;
	if (failed)
	{
		if (os_error_out)
			*os_error_out = errno;
		remove(temporary);
		return SG_RUNE_ARTIFACT_FILE_ERROR;
	}
	return SG_RUNE_ARTIFACT_OK;
}

int SG_RuneArtifactPath(char *output, size_t output_size,
	const char *game_directory, const char *map_name)
{
	int written;

	if (!output || !output_size)
		return 0;
	output[0] = '\0';
	if (!game_directory || !map_name || !map_name[0] ||
		strpbrk(map_name, "/\\") || strstr(map_name, ".."))
		return 0;
	written = snprintf(output, output_size, "%s/maps/%s.rune",
		game_directory[0] ? game_directory : ".", map_name);
	if (written < 0 || (size_t)written >= output_size)
	{
		output[0] = '\0';
		return 0;
	}
	return 1;
}

const char *SG_RuneArtifactStatusString(sg_rune_artifact_status_t status)
{
	switch (status)
	{
	case SG_RUNE_ARTIFACT_OK: return "ok";
	case SG_RUNE_ARTIFACT_INVALID_ARGUMENT: return "invalid argument";
	case SG_RUNE_ARTIFACT_OUT_OF_MEMORY: return "out of memory";
	case SG_RUNE_ARTIFACT_FILE_ERROR: return "file error";
	case SG_RUNE_ARTIFACT_BAD_MAGIC: return "not a RUNE";
	case SG_RUNE_ARTIFACT_BAD_VERSION: return "unsupported version";
	case SG_RUNE_ARTIFACT_TRUNCATED: return "truncated";
	case SG_RUNE_ARTIFACT_BAD_CHECKSUM: return "checksum mismatch";
	case SG_RUNE_ARTIFACT_BAD_SECTION: return "bad section";
	case SG_RUNE_ARTIFACT_BAD_RECORDS: return "bad records";
	case SG_RUNE_ARTIFACT_IDENTITY_MISMATCH: return "identity mismatch";
	case SG_RUNE_ARTIFACT_LAW_MISMATCH: return "law mismatch";
	default: return "unknown";
	}
}
