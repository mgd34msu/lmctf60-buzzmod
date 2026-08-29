/* Quake II IBSP v38 reader and owned static-world representation. */
#ifndef SG_BSP_WORLD_H
#define SG_BSP_WORLD_H

#include <stddef.h>
#include <stdint.h>

#define SG_BSP_VERSION UINT32_C(38)
#define SG_BSP_LUMP_COUNT UINT32_C(19)
#define SG_BSP_TEXTURE_NAME_BYTES UINT32_C(32)
#define SG_BSP_LIGHT_STYLE_COUNT UINT32_C(4)
#define SG_BSP_VISIBILITY_SET_COUNT UINT32_C(2)
/* Exact limits enforced by the selected q2repro host's IBSP loader. */
#define SG_BSP_MAX_CLUSTERS UINT32_C(65536)
#define SG_BSP_MAX_AREAS UINT32_C(256)
#define SG_BSP_MAX_MODELS UINT32_C(8190)
#define SG_BSP_CONTENT_ID_BYTES UINT32_C(32)

/* SHA-256 of the exact bytes accepted by SG_BspWorldLoadMemory/File. */
typedef struct sg_bsp_content_identity_s
{
	uint8_t bytes[SG_BSP_CONTENT_ID_BYTES];
} sg_bsp_content_identity_t;

typedef enum sg_bsp_lump_e
{
	SG_BSP_LUMP_ENTITIES = 0,
	SG_BSP_LUMP_PLANES,
	SG_BSP_LUMP_VERTICES,
	SG_BSP_LUMP_VISIBILITY,
	SG_BSP_LUMP_NODES,
	SG_BSP_LUMP_TEXINFO,
	SG_BSP_LUMP_FACES,
	SG_BSP_LUMP_LIGHTING,
	SG_BSP_LUMP_LEAVES,
	SG_BSP_LUMP_LEAF_FACES,
	SG_BSP_LUMP_LEAF_BRUSHES,
	SG_BSP_LUMP_EDGES,
	SG_BSP_LUMP_SURFEDGES,
	SG_BSP_LUMP_MODELS,
	SG_BSP_LUMP_BRUSHES,
	SG_BSP_LUMP_BRUSH_SIDES,
	SG_BSP_LUMP_POP,
	SG_BSP_LUMP_AREAS,
	SG_BSP_LUMP_AREAPORTALS
} sg_bsp_lump_t;

typedef enum sg_bsp_error_code_e
{
	SG_BSP_ERROR_NONE = 0,
	SG_BSP_ERROR_INVALID_ARGUMENT,
	SG_BSP_ERROR_IO,
	SG_BSP_ERROR_TRUNCATED_HEADER,
	SG_BSP_ERROR_BAD_MAGIC,
	SG_BSP_ERROR_UNSUPPORTED_VERSION,
	SG_BSP_ERROR_BAD_LUMP,
	SG_BSP_ERROR_SIZE_OVERFLOW,
	SG_BSP_ERROR_LIMIT_EXCEEDED,
	SG_BSP_ERROR_OUT_OF_MEMORY,
	SG_BSP_ERROR_NONFINITE_GEOMETRY,
	SG_BSP_ERROR_INVALID_GEOMETRY,
	SG_BSP_ERROR_INVALID_REFERENCE,
	SG_BSP_ERROR_INVALID_VISIBILITY,
	SG_BSP_ERROR_INVALID_TREE,
	SG_BSP_ERROR_INVALID_ANIMATION
} sg_bsp_error_code_t;

typedef struct sg_bsp_error_s
{
	sg_bsp_error_code_t code;
	sg_bsp_lump_t lump;
	uint32_t record;
} sg_bsp_error_t;

typedef struct sg_bsp_vec3_s
{
	float value[3];
} sg_bsp_vec3_t;

typedef struct sg_bsp_short_bounds_s
{
	int16_t mins[3];
	int16_t maxs[3];
} sg_bsp_short_bounds_t;

typedef struct sg_bsp_plane_s
{
	sg_bsp_vec3_t normal;
	float distance;
	int32_t type;
} sg_bsp_plane_t;

typedef struct sg_bsp_vertex_s
{
	sg_bsp_vec3_t point;
} sg_bsp_vertex_t;

typedef struct sg_bsp_visibility_s
{
	uint32_t cluster_count;
	uint32_t (*bit_offsets)[SG_BSP_VISIBILITY_SET_COUNT];
	uint8_t *bytes;
	uint32_t byte_count;
} sg_bsp_visibility_t;

typedef struct sg_bsp_node_s
{
	uint32_t plane;
	int32_t children[2];
	sg_bsp_short_bounds_t bounds;
	uint32_t first_face;
	uint32_t face_count;
} sg_bsp_node_t;

typedef struct sg_bsp_texinfo_s
{
	float vectors[2][4];
	int32_t flags;
	int32_t value;
	uint8_t texture[SG_BSP_TEXTURE_NAME_BYTES];
	int32_t next_texinfo;
} sg_bsp_texinfo_t;

typedef struct sg_bsp_face_s
{
	uint32_t plane;
	uint32_t side;
	uint32_t first_surfedge;
	uint32_t surfedge_count;
	uint32_t texinfo;
	uint8_t light_styles[SG_BSP_LIGHT_STYLE_COUNT];
	int32_t light_offset;
} sg_bsp_face_t;

typedef struct sg_bsp_leaf_s
{
	int32_t contents;
	int32_t cluster;
	uint32_t area;
	sg_bsp_short_bounds_t bounds;
	uint32_t first_leaf_face;
	uint32_t leaf_face_count;
	uint32_t first_leaf_brush;
	uint32_t leaf_brush_count;
} sg_bsp_leaf_t;

typedef struct sg_bsp_edge_s
{
	uint32_t vertices[2];
} sg_bsp_edge_t;

typedef struct sg_bsp_model_s
{
	/* Collision bounds match the host: disk mins - 1, disk maxs + 1. */
	sg_bsp_vec3_t mins;
	sg_bsp_vec3_t maxs;
	sg_bsp_vec3_t origin;
	int32_t headnode;
	uint32_t first_face;
	uint32_t face_count;
} sg_bsp_model_t;

typedef struct sg_bsp_brush_s
{
	uint32_t first_side;
	uint32_t side_count;
	int32_t contents;
} sg_bsp_brush_t;

typedef struct sg_bsp_brush_side_s
{
	uint32_t plane;
	int32_t texinfo;
} sg_bsp_brush_side_t;

typedef struct sg_bsp_area_s
{
	uint32_t areaportal_count;
	uint32_t first_areaportal;
} sg_bsp_area_t;

typedef struct sg_bsp_areaportal_s
{
	uint32_t portal_number;
	uint32_t other_area;
} sg_bsp_areaportal_t;

typedef struct sg_bsp_world_s
{
	/* Authenticated by the BSP loader before any borrowed authority is built. */
	sg_bsp_content_identity_t content_identity;
	/* Exact Quake II CM_LoadMap checksum of the same retained file bytes. */
	uint32_t engine_checksum;
	uint8_t *entities;
	uint32_t entity_byte_count;
	sg_bsp_plane_t *planes;
	uint32_t plane_count;
	sg_bsp_vertex_t *vertices;
	uint32_t vertex_count;
	sg_bsp_visibility_t visibility;
	sg_bsp_node_t *nodes;
	uint32_t node_count;
	sg_bsp_texinfo_t *texinfos;
	uint32_t texinfo_count;
	sg_bsp_face_t *faces;
	uint32_t face_count;
	uint8_t *lighting;
	uint32_t lighting_byte_count;
	sg_bsp_leaf_t *leaves;
	uint32_t leaf_count;
	uint32_t *leaf_faces;
	uint32_t leaf_face_count;
	uint32_t *leaf_brushes;
	uint32_t leaf_brush_count;
	sg_bsp_edge_t *edges;
	uint32_t edge_count;
	int32_t *surfedges;
	uint32_t surfedge_count;
	sg_bsp_model_t *models;
	uint32_t model_count;
	sg_bsp_brush_t *brushes;
	uint32_t brush_count;
	sg_bsp_brush_side_t *brush_sides;
	uint32_t brush_side_count;
	sg_bsp_area_t *areas;
	uint32_t area_count;
	sg_bsp_areaportal_t *areaportals;
	uint32_t areaportal_count;
} sg_bsp_world_t;

/* On success, assigns a new owned world to an initially NULL output. */
int SG_BspWorldLoadMemory(const void *data, size_t size,
	sg_bsp_world_t **world_out, sg_bsp_error_t *error_out);
int SG_BspWorldLoadFile(const char *path, sg_bsp_world_t **world_out,
	sg_bsp_error_t *error_out);
void SG_BspWorldDestroy(sg_bsp_world_t *world);
const char *SG_BspWorldErrorString(sg_bsp_error_code_t code);

#endif
