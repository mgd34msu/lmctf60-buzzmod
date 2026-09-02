/* Era-4 cell complex records.
 *
 * The RUNE's geometry is a cell complex: convex free-space cells, the planar
 * facets bounding them, the incidences that attach a cell to a facet on one
 * side, the portals that join two cells across one facet, and the brush
 * polygons (source surfaces) the hook can bite.  Every record is plain,
 * fixed-layout, and indexed by array position; SG_RUNE_CX_INDEX_NONE marks
 * an absent reference.  Coordinates are Q8 fixed point (units of 1/8). */
#ifndef SG_RUNE_CX_H
#define SG_RUNE_CX_H

#include <stdint.h>

#define SG_RUNE_CX_INDEX_NONE UINT32_MAX
#define SG_RUNE_CX_Q8_ONE 8

typedef struct sg_rune_cx_vec3_s
{
	int32_t value[3];
} sg_rune_cx_vec3_t;

typedef struct sg_rune_cx_bounds_s
{
	sg_rune_cx_vec3_t mins;
	sg_rune_cx_vec3_t maxs;
} sg_rune_cx_bounds_t;

/* IEEE binary32 bit patterns; n . p = d. */
typedef struct sg_rune_cx_plane_s
{
	uint32_t normal_bits[3];
	uint32_t distance_bits;
} sg_rune_cx_plane_t;

typedef struct sg_rune_cx_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_cx_span_t;

/* ---- provenance ----------------------------------------------------------- */

typedef enum sg_rune_cx_source_kind_e
{
	SG_RUNE_CX_SOURCE_DOMAIN = 0,
	SG_RUNE_CX_SOURCE_BSP_PLANE,
	SG_RUNE_CX_SOURCE_EXPANDED_BRUSH_SIDE,
	SG_RUNE_CX_SOURCE_SPLIT,
	SG_RUNE_CX_SOURCE_KIND_COUNT
} sg_rune_cx_source_kind_t;

typedef struct sg_rune_cx_domain_source_s
{
	uint32_t axis;
	uint32_t maximum_side;
} sg_rune_cx_domain_source_t;

typedef struct sg_rune_cx_bsp_plane_source_s
{
	uint32_t model;
	uint32_t leaf;
	uint32_t plane;
} sg_rune_cx_bsp_plane_source_t;

typedef struct sg_rune_cx_brush_side_source_s
{
	uint32_t model;
	uint32_t brush;
	uint32_t brush_side;
	uint32_t plane;
} sg_rune_cx_brush_side_source_t;

typedef struct sg_rune_cx_split_source_s
{
	uint32_t parent_facet;
	uint32_t ordinal;
} sg_rune_cx_split_source_t;

typedef struct sg_rune_cx_source_s
{
	uint32_t kind;            /* sg_rune_cx_source_kind_t */
	union
	{
		sg_rune_cx_domain_source_t domain;
		sg_rune_cx_bsp_plane_source_t bsp_plane;
		sg_rune_cx_brush_side_source_t brush_side;
		sg_rune_cx_split_source_t split;
	};
} sg_rune_cx_source_t;

typedef struct sg_rune_cx_cell_source_s
{
	uint32_t model;
	uint32_t leaf;
	uint32_t area;
	int32_t cluster;
	uint32_t split_ordinal;
} sg_rune_cx_cell_source_t;

/* ---- cells ---------------------------------------------------------------- */

typedef uint8_t sg_rune_cx_stances_t;
enum
{
	SG_RUNE_CX_STANCE_STANDING = UINT8_C(1) << 0,
	SG_RUNE_CX_STANCE_CROUCHING = UINT8_C(1) << 1
};
#define SG_RUNE_CX_STANCE_ALL \
	(SG_RUNE_CX_STANCE_STANDING | SG_RUNE_CX_STANCE_CROUCHING)

typedef uint32_t sg_rune_cx_contents_t;
enum
{
	SG_RUNE_CX_CONTENTS_SOLID = UINT32_C(1) << 0,
	SG_RUNE_CX_CONTENTS_WINDOW = UINT32_C(1) << 1,
	SG_RUNE_CX_CONTENTS_WATER = UINT32_C(1) << 2,
	SG_RUNE_CX_CONTENTS_LAVA = UINT32_C(1) << 3,
	SG_RUNE_CX_CONTENTS_SLIME = UINT32_C(1) << 4,
	SG_RUNE_CX_CONTENTS_PLAYER_CLIP = UINT32_C(1) << 5,
	SG_RUNE_CX_CONTENTS_SKY = UINT32_C(1) << 6,
	SG_RUNE_CX_CONTENTS_CURRENT_0 = UINT32_C(1) << 7,
	SG_RUNE_CX_CONTENTS_CURRENT_90 = UINT32_C(1) << 8,
	SG_RUNE_CX_CONTENTS_CURRENT_180 = UINT32_C(1) << 9,
	SG_RUNE_CX_CONTENTS_CURRENT_270 = UINT32_C(1) << 10,
	SG_RUNE_CX_CONTENTS_CURRENT_UP = UINT32_C(1) << 11,
	SG_RUNE_CX_CONTENTS_CURRENT_DOWN = UINT32_C(1) << 12
};

typedef uint32_t sg_rune_cx_semantics_t;
enum
{
	SG_RUNE_CX_CELL_HAZARD = UINT32_C(1) << 0,
	SG_RUNE_CX_CELL_SKY_BOUNDARY = UINT32_C(1) << 1,
	SG_RUNE_CX_CELL_VOID_BOUNDARY = UINT32_C(1) << 2,
	SG_RUNE_CX_CELL_MOVER_VOLUME = UINT32_C(1) << 3,
	SG_RUNE_CX_CELL_SUPPORTED = UINT32_C(1) << 4,   /* a floor within reach */
	SG_RUNE_CX_CELL_WATER = UINT32_C(1) << 5
};

typedef struct sg_rune_cx_cell_s
{
	sg_rune_cx_cell_source_t source;
	sg_rune_cx_bounds_t bounds;
	sg_rune_cx_span_t incidences;   /* into cell_incidences */
	sg_rune_cx_contents_t contents;
	sg_rune_cx_semantics_t semantics;
	sg_rune_cx_stances_t valid_stances;
	uint8_t reserved[3];
} sg_rune_cx_cell_t;

/* ---- facets, incidences, portals ------------------------------------------ */

typedef enum sg_rune_cx_side_e
{
	SG_RUNE_CX_NEGATIVE_SIDE = 0,   /* the cell lies where n . p <= d */
	SG_RUNE_CX_POSITIVE_SIDE,
	SG_RUNE_CX_SIDE_COUNT
} sg_rune_cx_side_t;

typedef enum sg_rune_cx_boundary_e
{
	SG_RUNE_CX_BOUNDARY_OPEN = 0,   /* the facet does not stop movement */
	SG_RUNE_CX_BOUNDARY_CLOSED,
	SG_RUNE_CX_BOUNDARY_COUNT
} sg_rune_cx_boundary_t;

typedef enum sg_rune_cx_facet_kind_e
{
	SG_RUNE_CX_FACET_POLYGON = 0,
	SG_RUNE_CX_FACET_CONSTRAINT_ONLY,
	SG_RUNE_CX_FACET_KIND_COUNT
} sg_rune_cx_facet_kind_t;

typedef struct sg_rune_cx_facet_s
{
	sg_rune_cx_source_t source;
	sg_rune_cx_plane_t plane;
	sg_rune_cx_span_t vertices;     /* into vertices */
	sg_rune_cx_span_t incidences;   /* into incidences */
	uint32_t portal;                /* or INDEX_NONE */
	uint32_t kind;                  /* sg_rune_cx_facet_kind_t */
} sg_rune_cx_facet_t;

typedef struct sg_rune_cx_incidence_s
{
	uint32_t cell;
	uint32_t facet;
	uint32_t cell_ordinal;
	uint32_t side;                  /* sg_rune_cx_side_t */
	uint32_t boundary;              /* sg_rune_cx_boundary_t */
} sg_rune_cx_incidence_t;

/* A directed crossing: from the cell of source_incidence to the cell of
 * destination_incidence through the shared facet.  The reverse crossing is
 * its own portal. */
typedef struct sg_rune_cx_portal_s
{
	sg_rune_cx_source_t source;
	uint32_t facet;
	uint32_t source_incidence;
	uint32_t destination_incidence;
	uint32_t clearance_q8;
	sg_rune_cx_stances_t valid_stances;
	uint8_t reserved[3];
} sg_rune_cx_portal_t;

/* ---- source surfaces ------------------------------------------------------ */

typedef enum sg_rune_cx_surface_frame_e
{
	SG_RUNE_CX_SURFACE_WORLD = 0,
	SG_RUNE_CX_SURFACE_MODEL_LOCAL,
	SG_RUNE_CX_SURFACE_FRAME_COUNT
} sg_rune_cx_surface_frame_t;

typedef struct sg_rune_cx_surface_s
{
	sg_rune_cx_brush_side_source_t source;
	uint32_t frame;                 /* sg_rune_cx_surface_frame_t */
	uint32_t cell;                  /* or INDEX_NONE */
	uint32_t parent_surface;
	uint32_t split_ordinal;
	sg_rune_cx_plane_t plane;
	sg_rune_cx_span_t vertices;     /* into surface_vertices */
} sg_rune_cx_surface_t;

/* ---- the complex as borrowed arrays --------------------------------------- */

typedef struct sg_rune_cx_view_s
{
	const sg_rune_cx_cell_t *cells;
	uint32_t cell_count;
	const sg_rune_cx_facet_t *facets;
	uint32_t facet_count;
	const sg_rune_cx_incidence_t *incidences;
	uint32_t incidence_count;
	const uint32_t *cell_incidences;
	uint32_t cell_incidence_count;
	const sg_rune_cx_vec3_t *vertices;
	uint32_t vertex_count;
	const sg_rune_cx_portal_t *portals;
	uint32_t portal_count;
	const sg_rune_cx_surface_t *surfaces;
	uint32_t surface_count;
	const sg_rune_cx_vec3_t *surface_vertices;
	uint32_t surface_vertex_count;
} sg_rune_cx_view_t;

/* Where validation failed: which array, which record, and why. */
typedef struct sg_rune_fault_s
{
	const char *array;
	uint32_t record;
	const char *reason;
} sg_rune_fault_t;

/* Every reference in range, every span inside its array, every portal's two
 * incidences on opposite sides of its facet.  fault_out may be NULL. */
int SG_RuneCxViewValid(const sg_rune_cx_view_t *view,
	sg_rune_fault_t *fault_out);

#endif /* SG_RUNE_CX_H */
