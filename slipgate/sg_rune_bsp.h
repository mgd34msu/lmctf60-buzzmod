/* Era-4 BSP: the map as the RUNE reads it.
 *
 * One load reads exactly the lumps the RUNE uses (planes, nodes, leaves,
 * leaf brushes, models, brushes, brush sides, texinfo, visibility, the
 * entity text) into one arena, validates every reference on the way in,
 * and computes the CRC of the file and of the entity text for the
 * identity.  Faces, vertices, edges, lighting and areas are not read: the
 * RUNE is carved from brushes and reads the tree, never the render mesh.
 * Faults name the lump and the record. */
#ifndef SG_RUNE_BSP_H
#define SG_RUNE_BSP_H

#include <stddef.h>
#include <stdint.h>

/* Q2 contents bits, as the brushes carry them. */
#define SG_RUNE_CONTENTS_SOLID        0x00000001
#define SG_RUNE_CONTENTS_WINDOW       0x00000002
#define SG_RUNE_CONTENTS_AUX          0x00000004
#define SG_RUNE_CONTENTS_LAVA         0x00000008
#define SG_RUNE_CONTENTS_SLIME        0x00000010
#define SG_RUNE_CONTENTS_WATER        0x00000020
#define SG_RUNE_CONTENTS_MIST         0x00000040
#define SG_RUNE_CONTENTS_PLAYERCLIP   0x00010000
#define SG_RUNE_CONTENTS_MONSTERCLIP  0x00020000
#define SG_RUNE_CONTENTS_CURRENT_0    0x00040000
#define SG_RUNE_CONTENTS_CURRENT_90   0x00080000
#define SG_RUNE_CONTENTS_CURRENT_180  0x00100000
#define SG_RUNE_CONTENTS_CURRENT_270  0x00200000
#define SG_RUNE_CONTENTS_CURRENT_UP   0x00400000
#define SG_RUNE_CONTENTS_CURRENT_DOWN 0x00800000
#define SG_RUNE_CONTENTS_ORIGIN       0x01000000
#define SG_RUNE_CONTENTS_MONSTER      0x02000000
#define SG_RUNE_CONTENTS_DEADMONSTER  0x04000000
#define SG_RUNE_CONTENTS_DETAIL       0x08000000
#define SG_RUNE_CONTENTS_TRANSLUCENT  0x10000000
#define SG_RUNE_CONTENTS_LADDER       0x20000000

#define SG_RUNE_MASK_SOLID (SG_RUNE_CONTENTS_SOLID | SG_RUNE_CONTENTS_WINDOW)
#define SG_RUNE_MASK_PLAYER_SOLID (SG_RUNE_CONTENTS_SOLID | SG_RUNE_CONTENTS_PLAYERCLIP | \
	SG_RUNE_CONTENTS_WINDOW | SG_RUNE_CONTENTS_MONSTER)
#define SG_RUNE_MASK_WATER (SG_RUNE_CONTENTS_WATER | SG_RUNE_CONTENTS_LAVA | \
	SG_RUNE_CONTENTS_SLIME)
#define SG_RUNE_MASK_OPAQUE (SG_RUNE_CONTENTS_SOLID | SG_RUNE_CONTENTS_SLIME | \
	SG_RUNE_CONTENTS_LAVA)
#define SG_RUNE_MASK_SHOT (SG_RUNE_CONTENTS_SOLID | SG_RUNE_CONTENTS_MONSTER | \
	SG_RUNE_CONTENTS_WINDOW | SG_RUNE_CONTENTS_DEADMONSTER)

/* Q2 surface flags on texinfo. */
#define SG_RUNE_SURF_LIGHT   0x1
#define SG_RUNE_SURF_SLICK   0x2
#define SG_RUNE_SURF_SKY     0x4
#define SG_RUNE_SURF_WARP    0x8
#define SG_RUNE_SURF_TRANS33 0x10
#define SG_RUNE_SURF_TRANS66 0x20
#define SG_RUNE_SURF_FLOWING 0x40
#define SG_RUNE_SURF_NODRAW  0x80

#define SG_RUNE_BSP_TEXTURE_BYTES 32

typedef struct sg_rune_bsp_plane_s
{
	float normal[3];
	float distance;
} sg_rune_bsp_plane_t;

typedef struct sg_rune_bsp_node_s
{
	uint32_t plane;
	int32_t children[2];      /* >= 0 node; < 0 leaf as -1 - index */
} sg_rune_bsp_node_t;

typedef struct sg_rune_bsp_leaf_s
{
	int32_t contents;
	int32_t cluster;          /* -1 when outside the visibility */
	int32_t area;
	float mins[3], maxs[3];   /* the leaf's box, from the file's shorts */
	uint32_t first_leaf_brush;
	uint32_t leaf_brush_count;
} sg_rune_bsp_leaf_t;

typedef struct sg_rune_bsp_model_s
{
	float mins[3];
	float maxs[3];
	float origin[3];
	int32_t headnode;
} sg_rune_bsp_model_t;

typedef struct sg_rune_bsp_brush_s
{
	uint32_t first_side;
	uint32_t side_count;
	int32_t contents;
} sg_rune_bsp_brush_t;

typedef struct sg_rune_bsp_side_s
{
	uint32_t plane;
	int32_t texinfo;          /* -1 for none */
} sg_rune_bsp_side_t;

typedef struct sg_rune_bsp_texinfo_s
{
	int32_t flags;
	int32_t value;
	char texture[SG_RUNE_BSP_TEXTURE_BYTES];
} sg_rune_bsp_texinfo_t;

typedef struct sg_rune_bsp_visibility_s
{
	uint32_t cluster_count;   /* 0 when the map has no visibility data */
	const uint32_t *pvs_offsets; /* per cluster: byte offset of its run-length row */
	const uint8_t *bytes;
	uint32_t byte_count;
} sg_rune_bsp_visibility_t;

/* An inline model that stands in the world from spawn on (a func_wall
 * that starts solid, a func_explosive not yet blown): the world as the
 * RUNE reads it is model 0 plus these, each at its origin. */
typedef struct sg_rune_bsp_static_s
{
	uint32_t model;
	float origin[3];
} sg_rune_bsp_static_t;

typedef struct sg_rune_bsp_s
{
	const sg_rune_bsp_plane_t *planes;
	uint32_t plane_count;
	const sg_rune_bsp_node_t *nodes;
	uint32_t node_count;
	const sg_rune_bsp_leaf_t *leaves;
	uint32_t leaf_count;
	const uint32_t *leaf_brushes;
	uint32_t leaf_brush_count;
	const sg_rune_bsp_model_t *models;
	uint32_t model_count;
	const sg_rune_bsp_brush_t *brushes;
	uint32_t brush_count;
	const sg_rune_bsp_side_t *sides;
	uint32_t side_count;
	const sg_rune_bsp_texinfo_t *texinfos;
	uint32_t texinfo_count;
	sg_rune_bsp_visibility_t visibility;
	const char *entities;     /* the entity text, NUL-terminated */
	uint32_t entity_bytes;    /* without the NUL */
	uint32_t file_crc32;
	uint32_t entity_crc32;
	uint64_t file_bytes;
	const sg_rune_bsp_static_t *statics; /* models standing from spawn */
	uint32_t static_count;
	void *arena;              /* everything above lives here */
	char *entities_owned;     /* a replaced entity text, or NULL */
	void *statics_owned;      /* the statics array, or NULL */
} sg_rune_bsp_t;

typedef enum sg_rune_bsp_lump_e
{
	SG_RUNE_BSP_LUMP_ENTITIES = 0,
	SG_RUNE_BSP_LUMP_PLANES = 1,
	SG_RUNE_BSP_LUMP_VISIBILITY = 3,
	SG_RUNE_BSP_LUMP_NODES = 4,
	SG_RUNE_BSP_LUMP_TEXINFO = 5,
	SG_RUNE_BSP_LUMP_LEAVES = 8,
	SG_RUNE_BSP_LUMP_LEAF_BRUSHES = 10,
	SG_RUNE_BSP_LUMP_MODELS = 13,
	SG_RUNE_BSP_LUMP_BRUSHES = 14,
	SG_RUNE_BSP_LUMP_BRUSH_SIDES = 15,
	SG_RUNE_BSP_LUMP_COUNT = 19
} sg_rune_bsp_lump_t;

typedef struct sg_rune_bsp_fault_s
{
	const char *what;         /* "io", "magic", "version", "lump", "reference", "memory" */
	int lump;                 /* the lump concerned, or -1 */
	uint32_t record;          /* the record concerned, or 0 */
} sg_rune_bsp_fault_t;

int SG_RuneBspLoadFile(const char *path, sg_rune_bsp_t *bsp_out,
	sg_rune_bsp_fault_t *fault_out);
int SG_RuneBspLoadImage(const uint8_t *image, size_t size, sg_rune_bsp_t *bsp_out,
	sg_rune_bsp_fault_t *fault_out);
void SG_RuneBspFree(sg_rune_bsp_t *bsp);

/* Replaces the entity text (the game may spawn from an override file);
 * the entity CRC follows. */
int SG_RuneBspReplaceEntities(sg_rune_bsp_t *bsp, const char *text);

/* Declares the models that stand in the world from spawn: model 0 traces
 * and the carve include them.  A copy is kept; count 0 clears. */
int SG_RuneBspSetStatics(sg_rune_bsp_t *bsp, const sg_rune_bsp_static_t *statics,
	uint32_t count);

/* The leaf of a model's tree holding a point (model 0 is the world). */
int32_t SG_RuneBspLeafAt(const sg_rune_bsp_t *bsp, uint32_t model,
	const float point[3]);

#endif /* SG_RUNE_BSP_H */
