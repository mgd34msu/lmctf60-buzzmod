/* sg_rune_seed_game.c -- exact host-backed RUNE seed canonicalization. */

#include "../g_local.h"
#include "sg_local.h"
#include "sg_hooks.h"
#include "sg_rune_proof.h"
#include "sg_rune_seed_game.h"
#include "sg_rune_topology_game.h"

#include <limits.h>

#define SEED_STEP_MSEC 25
#define BSP_HEADER_BYTES (8U + 19U * 8U)
#define BSP_PLANE_LUMP 1U
#define BSP_VERTEX_LUMP 2U
#define BSP_TEXINFO_LUMP 5U
#define BSP_FACE_LUMP 6U
#define BSP_EDGE_LUMP 11U
#define BSP_SURFEDGE_LUMP 12U
#define BSP_MODEL_LUMP 13U
#define BSP_PLANE_BYTES 20U
#define BSP_VERTEX_BYTES 12U
#define BSP_TEXINFO_BYTES 76U
#define BSP_FACE_BYTES 20U
#define BSP_EDGE_BYTES 4U
#define BSP_SURFEDGE_BYTES 4U
#define BSP_MODEL_BYTES 48U

_Static_assert(SEED_STEP_MSEC == SG_RUNE_PROOF_PMOVE_SUBSTEP_MS,
	"seed stability cadence drift");

static uint32_t RuneSeed_ReadLittleU32(const unsigned char bytes[4])
{
	return (uint32_t)bytes[0] |
		((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) |
		((uint32_t)bytes[3] << 24);
}

static float RuneSeed_ReadLittleFloat(const unsigned char bytes[4])
{
	uint32_t bits = RuneSeed_ReadLittleU32(bytes);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int32_t RuneSeed_ReadLittleI32(const unsigned char bytes[4])
{
	return (int32_t)RuneSeed_ReadLittleU32(bytes);
}

static uint16_t RuneSeed_ReadLittleU16(const unsigned char bytes[2])
{
	return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static int16_t RuneSeed_ReadLittleI16(const unsigned char bytes[2])
{
	return (int16_t)RuneSeed_ReadLittleU16(bytes);
}

typedef struct rune_seed_bsp_lump_s
{
	size_t offset;
	size_t count;
} rune_seed_bsp_lump_t;

static qboolean RuneSeed_BspLump(const unsigned char *header,
	size_t file_length, uint32_t index, size_t stride,
	rune_seed_bsp_lump_t *lump)
{
	uint32_t offset, length;

	if (!header || !stride || !lump || index >= 19U)
		return false;
	offset = RuneSeed_ReadLittleU32(header + 8U + index * 8U);
	length = RuneSeed_ReadLittleU32(header + 12U + index * 8U);
	if (offset < BSP_HEADER_BYTES || length % stride != 0U ||
	    (uint64_t)offset + (uint64_t)length > (uint64_t)file_length)
		return false;
	lump->offset = offset;
	lump->count = length / stride;
	return true;
}

static qboolean RuneSeed_ReadBspWorldBounds(const char *path, vec3_t mins,
	vec3_t maxs)
{
	unsigned char header[BSP_HEADER_BYTES];
	unsigned char model[BSP_MODEL_BYTES];
	uint32_t model_offset, model_length;
	long file_length;
	FILE *file;

	if (!path || !path[0] || !mins || !maxs ||
	    !(file = fopen(path, "rb")))
		return false;
	if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
	    memcmp(header, "IBSP", 4) != 0 ||
	    RuneSeed_ReadLittleU32(header + 4) != 38U)
		goto fail;
	model_offset = RuneSeed_ReadLittleU32(
		header + 8U + BSP_MODEL_LUMP * 8U);
	model_length = RuneSeed_ReadLittleU32(
		header + 12U + BSP_MODEL_LUMP * 8U);
	if (model_offset < BSP_HEADER_BYTES || model_length < BSP_MODEL_BYTES ||
	    model_length % BSP_MODEL_BYTES != 0U ||
	    fseek(file, 0, SEEK_END) != 0 ||
	    (file_length = ftell(file)) < 0 ||
	    (uint64_t)model_offset + (uint64_t)model_length >
	        (uint64_t)file_length ||
	    (uint64_t)model_offset > (uint64_t)LONG_MAX ||
	    fseek(file, (long)model_offset, SEEK_SET) != 0 ||
	    fread(model, 1, sizeof(model), file) != sizeof(model))
		goto fail;
	for (int axis = 0; axis < 3; axis++)
	{
		/* CM_LoadMap exposes the same one-unit expansion for model bounds. */
		mins[axis] = RuneSeed_ReadLittleFloat(model + axis * 4U) - 1.0f;
		maxs[axis] = RuneSeed_ReadLittleFloat(model + (axis + 3) * 4U) + 1.0f;
		if (!isfinite(mins[axis]) || !isfinite(maxs[axis]) ||
		    mins[axis] > maxs[axis])
			goto fail;
	}
	fclose(file);
	return true;

fail:
	fclose(file);
	return false;
}

qboolean SG_RuneSeedReadMapWorldBounds(const char *game_directory,
	const char *mapname, vec3_t mins, vec3_t maxs)
{
	char path[MAX_OSPATH];
	int written;

	if (!game_directory || !game_directory[0] || !mapname || !mapname[0])
		return false;
	written = snprintf(path, sizeof(path), "%s/maps/%s.bsp",
		game_directory, mapname);
	return written >= 0 && (size_t)written < sizeof(path) &&
		RuneSeed_ReadBspWorldBounds(path, mins, maxs);
}

qboolean SG_RuneSeedGround(vec3_t candidate, vec3_t out)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	vec3_t start, down;
	trace_t trace;
	static const float lifts[] = { 0, 8, 24, 40, 56 };
	int lift;

	for (lift = 0; lift < 5; lift++)
	{
		VectorCopy(candidate, start);
		start[2] += lifts[lift];
		VectorCopy(start, down);
		down[2] -= 128.0f + lifts[lift];
		trace = sg_host.trace(start, mins, maxs, down, NULL,
			MASK_PLAYERSOLID);
		if (!trace.startsolid && !trace.allsolid &&
		    !(trace.ent && trace.ent->solid == SOLID_BSP &&
		      trace.ent->classname &&
		      !strcmp(trace.ent->classname, "func_rotating")) &&
		    !SG_OracleRotatorSweepBlocks(start, mins, maxs, trace.endpos,
		        MASK_PLAYERSOLID))
			break;
	}
	if (lift == 5 || trace.fraction == 1.0f || trace.plane.normal[2] < 0.7f)
		return false;
	return SG_OracleCanonicalGroundSource(trace.endpos, out);
}

static qboolean RuneSeed_SurfaceAt(const vec3_t sample, qboolean crouched,
	vec3_t out)
{
	static const vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	vec3_t down;
	trace_t trace;

	if (crouched)
		maxs[2] = 4.0f;
	VectorCopy(sample, down);
	down[2] -= 32.0f;
	trace = sg_host.trace((vec_t *)sample, (vec_t *)mins, (vec_t *)maxs,
		down, NULL, MASK_PLAYERSOLID);
	if (trace.startsolid || trace.allsolid || trace.fraction >= 1.0f ||
		trace.plane.normal[2] < 0.7f ||
		(trace.ent && trace.ent->solid == SOLID_BSP &&
		 trace.ent->classname &&
		 !strcmp(trace.ent->classname, "func_rotating")) ||
		SG_OracleRotatorSweepBlocks(sample, mins, maxs, trace.endpos,
			MASK_PLAYERSOLID))
		return false;
	return SG_OracleCanonicalGroundSourcePose(trace.endpos, crouched, out);
}

static qboolean RuneSeed_FaceVertex(const unsigned char *data,
	const rune_seed_bsp_lump_t *vertices,
	const rune_seed_bsp_lump_t *edges,
	const rune_seed_bsp_lump_t *surfedges, int32_t firstedge,
	uint32_t index, vec3_t point)
{
	int32_t directed;
	uint32_t edge_index, vertex_index;
	const unsigned char *edge, *vertex;

	if (firstedge < 0 || (uint64_t)(uint32_t)firstedge + index >=
	    surfedges->count)
		return false;
	directed = RuneSeed_ReadLittleI32(data + surfedges->offset +
		((size_t)(uint32_t)firstedge + index) * BSP_SURFEDGE_BYTES);
	if (directed == INT32_MIN)
		return false;
	edge_index = directed < 0 ? (uint32_t)-directed : (uint32_t)directed;
	if (edge_index >= edges->count)
		return false;
	edge = data + edges->offset + (size_t)edge_index * BSP_EDGE_BYTES;
	vertex_index = RuneSeed_ReadLittleU16(edge + (directed < 0 ? 2U : 0U));
	if (vertex_index >= vertices->count)
		return false;
	vertex = data + vertices->offset + (size_t)vertex_index * BSP_VERTEX_BYTES;
	for (int axis = 0; axis < 3; axis++)
	{
		point[axis] = RuneSeed_ReadLittleFloat(vertex + (size_t)axis * 4U);
		if (!isfinite(point[axis]))
			return false;
	}
	return true;
}

static qboolean RuneSeed_EmitFaceAnchor(const vec3_t point,
	sg_rune_seed_surface_emit_fn emit, void *context, uint64_t *scanned)
{
	vec3_t sample, surface;
	qboolean crouched = false;

	VectorCopy(point, sample);
	sample[2] += 25.0f;
	(*scanned)++;
	if (!RuneSeed_SurfaceAt(sample, false, surface))
	{
		if (!RuneSeed_SurfaceAt(sample, true, surface))
			return true;
		crouched = true;
	}
	return emit(context, surface, crouched);
}

static qboolean RuneSeed_ScanBspFaceAnchors(const char *path,
	sg_rune_seed_surface_emit_fn emit, void *context, uint64_t *scan_count)
{
	rune_seed_bsp_lump_t planes, vertices, texinfos, faces, edges, surfedges;
	rune_seed_bsp_lump_t models;
	unsigned char header[BSP_HEADER_BYTES], *data = NULL;
	uint64_t scanned = 0U;
	long length;
	FILE *file = NULL;
	qboolean result = false;

	if (scan_count) *scan_count = 0U;
	if (!path || !path[0] || !emit || !(file = fopen(path, "rb")) ||
	    fread(header, 1, sizeof(header), file) != sizeof(header) ||
	    memcmp(header, "IBSP", 4) != 0 ||
	    RuneSeed_ReadLittleU32(header + 4) != 38U ||
	    fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
	    (uint64_t)length > SIZE_MAX || fseek(file, 0, SEEK_SET) != 0)
		goto done;
	if (!RuneSeed_BspLump(header, (size_t)length, BSP_PLANE_LUMP,
	        BSP_PLANE_BYTES, &planes) ||
	    !RuneSeed_BspLump(header, (size_t)length, BSP_VERTEX_LUMP,
	        BSP_VERTEX_BYTES, &vertices) ||
	    !RuneSeed_BspLump(header, (size_t)length, BSP_TEXINFO_LUMP,
	        BSP_TEXINFO_BYTES, &texinfos) ||
	    !RuneSeed_BspLump(header, (size_t)length, BSP_FACE_LUMP,
	        BSP_FACE_BYTES, &faces) ||
	    !RuneSeed_BspLump(header, (size_t)length, BSP_EDGE_LUMP,
	        BSP_EDGE_BYTES, &edges) ||
	    !RuneSeed_BspLump(header, (size_t)length, BSP_SURFEDGE_LUMP,
	        BSP_SURFEDGE_BYTES, &surfedges) ||
	    !RuneSeed_BspLump(header, (size_t)length, BSP_MODEL_LUMP,
	        BSP_MODEL_BYTES, &models) || !models.count)
		goto done;
	data = malloc((size_t)length);
	if (!data || fread(data, 1, (size_t)length, file) != (size_t)length)
		goto done;
	{
		const unsigned char *world_model = data + models.offset;
		int32_t firstface = RuneSeed_ReadLittleI32(world_model + 40U);
		int32_t numfaces = RuneSeed_ReadLittleI32(world_model + 44U);

		if (firstface < 0 || numfaces < 0 ||
		    (uint64_t)(uint32_t)firstface + (uint32_t)numfaces > faces.count)
			goto done;
		for (int32_t face_index = 0; face_index < numfaces; face_index++)
		{
			const unsigned char *face = data + faces.offset +
				(size_t)((uint32_t)firstface + (uint32_t)face_index) *
				BSP_FACE_BYTES;
			uint32_t plane_index = RuneSeed_ReadLittleU16(face);
			int16_t side = RuneSeed_ReadLittleI16(face + 2U);
			int32_t firstedge = RuneSeed_ReadLittleI32(face + 4U);
			int16_t numedges = RuneSeed_ReadLittleI16(face + 8U);
			int16_t texinfo = RuneSeed_ReadLittleI16(face + 10U);
			vec3_t centroid = { 0.0f, 0.0f, 0.0f }, point, next;
			const unsigned char *plane, *texture;
			float normal_z;

			if (plane_index >= planes.count || texinfo < 0 ||
			    (uint32_t)texinfo >= texinfos.count || numedges < 3 ||
			    (side != 0 && side != 1))
				goto done;
			plane = data + planes.offset + (size_t)plane_index * BSP_PLANE_BYTES;
			texture = data + texinfos.offset +
				(size_t)(uint16_t)texinfo * BSP_TEXINFO_BYTES;
			normal_z = (side ? -1.0f : 1.0f) *
				RuneSeed_ReadLittleFloat(plane + 8U);
			if (!isfinite(normal_z))
				goto done;
			if (normal_z < 0.7f ||
			    (RuneSeed_ReadLittleU32(texture + 32U) & SURF_SKY))
				continue;
			for (uint32_t edge = 0U; edge < (uint32_t)numedges; edge++)
			{
				if (!RuneSeed_FaceVertex(data, &vertices, &edges, &surfedges,
				        firstedge, edge, point))
					goto done;
				VectorAdd(centroid, point, centroid);
			}
			VectorScale(centroid, 1.0f / numedges, centroid);
			if (!RuneSeed_EmitFaceAnchor(centroid, emit, context, &scanned))
				goto done;
			for (uint32_t edge = 0U; edge < (uint32_t)numedges; edge++)
			{
				vec3_t anchor;

				if (!RuneSeed_FaceVertex(data, &vertices, &edges, &surfedges,
				        firstedge, edge, point) ||
				    !RuneSeed_FaceVertex(data, &vertices, &edges, &surfedges,
				        firstedge, (edge + 1U) % (uint32_t)numedges, next))
					goto done;
				VectorAdd(point, next, anchor);
				VectorScale(anchor, 0.5f, anchor);
				for (int axis = 0; axis < 3; axis++)
					anchor[axis] = anchor[axis] * 0.75f +
						centroid[axis] * 0.25f;
				if (!RuneSeed_EmitFaceAnchor(anchor, emit, context, &scanned))
					goto done;
			}
		}
	}
	result = true;

done:
	if (scan_count) *scan_count = scanned;
	free(data);
	if (file) fclose(file);
	return result;
}

qboolean SG_RuneSeedScanMapFaceAnchors(const char *game_directory,
	const char *mapname, sg_rune_seed_surface_emit_fn emit, void *context,
	uint64_t *scan_count)
{
	char path[MAX_OSPATH];
	int written;

	if (!game_directory || !game_directory[0] || !mapname || !mapname[0])
		return false;
	written = snprintf(path, sizeof(path), "%s/maps/%s.bsp",
		game_directory, mapname);
	return written >= 0 && (size_t)written < sizeof(path) &&
		RuneSeed_ScanBspFaceAnchors(path, emit, context, scan_count);
}

qboolean SG_RuneSeedScanWorldSurfaces(const vec3_t world_mins,
	const vec3_t world_maxs, sg_rune_seed_surface_emit_fn emit,
	void *context, uint64_t *scan_count)
{
	const float spacing = 32.0f;
	uint64_t scanned = 0U;
	vec3_t sample, surface;
	int64_t first[3], last[3];

	if (scan_count)
		*scan_count = 0U;
	if (!world_mins || !world_maxs || !emit || !sg_host.trace ||
	    !sg_host.pointcontents)
		return false;
	for (int axis = 0; axis < 3; axis++)
	{
		float low = floorf(world_mins[axis] / spacing);
		float high = ceilf(world_maxs[axis] / spacing) + 1.0f;

		if (!isfinite(low) || !isfinite(high) ||
		    low < (float)INT32_MIN || high > (float)INT32_MAX || low > high)
			return false;
		first[axis] = (int64_t)low;
		last[axis] = (int64_t)high;
	}
	for (int64_t z = first[2]; z <= last[2]; z++)
		for (int64_t y = first[1]; y <= last[1]; y++)
			for (int64_t x = first[0]; x <= last[0]; x++)
			{
				qboolean crouched = false;

				VectorSet(sample, (float)x * spacing, (float)y * spacing,
					(float)z * spacing);
				scanned++;
				if (sg_host.pointcontents(sample) & CONTENTS_SOLID)
					continue;
				if (!RuneSeed_SurfaceAt(sample, false, surface))
				{
					if (!RuneSeed_SurfaceAt(sample, true, surface))
						continue;
					crouched = true;
				}
				if (!emit(context, surface, crouched))
				{
					if (scan_count)
						*scan_count = scanned;
					return false;
				}
			}
	if (scan_count)
		*scan_count = scanned;
	return true;
}

qboolean SG_RuneSeedLocalContact(const vec3_t first, qboolean first_crouched,
	const vec3_t second, qboolean second_crouched)
{
	static const vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	trace_t trace;

	if (!first || !second || !sg_host.trace)
		return false;
	if (first[0] == second[0] && first[1] == second[1] &&
	    first[2] == second[2])
		return true;
	if (first_crouched || second_crouched)
		maxs[2] = 4.0f;
	trace = sg_host.trace((vec_t *)first, (vec_t *)mins, maxs,
	    (vec_t *)second, NULL, MASK_PLAYERSOLID);
	return !trace.startsolid && !trace.allsolid && trace.fraction >= 1.0f &&
	    !SG_OracleRotatorSweepBlocks(first, mins, maxs, second,
	        MASK_PLAYERSOLID);
}

qboolean SG_RuneSeedRecordBspOverlay(const rune_seed_t *seeds,
	const byte *crouched, int seed_count,
	const rune_mechanism_node_t *mechanisms, uint32_t mechanism_count,
	sg_rune_contact_ledger_t *ledger, uint64_t *pairs_examined,
	uint32_t *contacts_recorded)
{
	const float horizontal_reach2 = 96.0f * 96.0f;
	uint64_t examined = 0U;
	uint32_t recorded = 0U;

	if (pairs_examined) *pairs_examined = 0U;
	if (contacts_recorded) *contacts_recorded = 0U;
	if (!seeds || !crouched || seed_count <= 0 || !ledger ||
	    (mechanism_count && !mechanisms))
		return false;
	for (int first = 0; first < seed_count; first++)
		for (int second = first + 1; second < seed_count; second++)
		{
			vec3_t delta;
			uint32_t before;

			examined++;
			VectorSubtract(seeds[second].origin, seeds[first].origin, delta);
			if (delta[0] * delta[0] + delta[1] * delta[1] >
			        horizontal_reach2 || fabsf(delta[2]) > 96.0f ||
			    !SG_RuneSeedLocalContact(seeds[first].origin,
			        crouched[first] != 0, seeds[second].origin,
			        crouched[second] != 0))
				continue;
			before = ledger->contact_count;
			if (SG_RuneTopologyRecordContact(ledger, first, second,
			        SG_RUNE_CONTACT_BSP_OVERLAY,
			        SG_RuneTopologyGameContactKind(seeds, first, second,
			            mechanisms, mechanism_count)) != SG_RUNE_TOPOLOGY_OK)
				return false;
			if (ledger->contact_count != before)
				recorded++;
		}
	if (pairs_examined) *pairs_examined = examined;
	if (contacts_recorded) *contacts_recorded = recorded;
	return true;
}

static void RuneSeed_PlacePose(sg_phantom_t *phantom, vec3_t origin,
	qboolean crouched)
{
	usercmd_t command;

	SG_OraclePlace(phantom, origin);
	if (crouched)
		phantom->pms.pm_flags |= PMF_DUCKED;
	memset(&command, 0, sizeof(command));
	command.msec = 0;
	command.upmove = crouched ? -400 : 0;
	SG_OracleRun(phantom, &command, 1);
}

int SG_RuneSeedSourceWaterlevelPose(vec3_t origin, qboolean crouched,
	int *watertype)
{
	sg_phantom_t phantom;

	RuneSeed_PlacePose(&phantom, origin, crouched);
	if (watertype)
		*watertype = phantom.watertype;
	return phantom.waterlevel;
}

int SG_RuneSeedSourceWaterlevel(vec3_t origin, int *watertype)
{
	return SG_RuneSeedSourceWaterlevelPose(origin, false, watertype);
}

qboolean SG_RuneSeedTriggerSafePose(vec3_t origin, qboolean crouched)
{
	sg_phantom_t phantom;

	RuneSeed_PlacePose(&phantom, origin, crouched);
	return SG_OracleWorldTriggerClear(&phantom);
}

qboolean SG_RuneSeedTriggerSafe(vec3_t origin)
{
	return SG_RuneSeedTriggerSafePose(origin, false);
}

qboolean SG_RuneSeedSourceUnstablePose(vec3_t origin, qboolean crouched)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	vec3_t start, end;
	sg_phantom_t phantom;
	usercmd_t command;
	short fixed[3];
	trace_t trace;
	int axis, step;

	if (crouched)
		maxs[2] = 4.0f;
	VectorCopy(origin, start);
	VectorCopy(origin, end);
	start[2] += 1.0f;
	end[2] -= 4.0f;
	trace = sg_host.trace(start, mins, maxs, end, NULL, MASK_PLAYERSOLID);
	if (trace.fraction >= 1.0f || !trace.surface ||
	    (trace.surface->flags & SURF_SLICK) ||
	    (trace.contents & MASK_CURRENT))
		return true;
	RuneSeed_PlacePose(&phantom, origin, crouched);
	for (axis = 0; axis < 3; axis++)
		fixed[axis] = phantom.pms.origin[axis];
	memset(&command, 0, sizeof(command));
	command.msec = SEED_STEP_MSEC;
	command.upmove = crouched ? -400 : 0;
	for (step = 0; step < 4; step++)
		if (!SG_OracleRunWorld(&phantom, &command, 1))
			return true;
	if (!phantom.groundentity || phantom.waterlevel >= 2 ||
	    (phantom.watertype &
	     (MASK_CURRENT | CONTENTS_LAVA | CONTENTS_SLIME)))
		return true;
	for (axis = 0; axis < 3; axis++)
		if (phantom.pms.origin[axis] != fixed[axis] ||
		    phantom.pms.velocity[axis] != 0)
			return true;
	return false;
}

qboolean SG_RuneSeedSourceUnstable(vec3_t origin)
{
	return SG_RuneSeedSourceUnstablePose(origin, false);
}
