/* Locator, router, field, step, and flight over a synthetic complex:
 * three floor cells in a row along +x at z 0..64, and past the last one
 * a drop to a floor 64 lower through an air cell. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_rune_field.h"
#include "../slipgate/sg_rune_flight.h"
#include "../slipgate/sg_rune_locate.h"

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { failures++; \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

#define MAX_CELLS 8
#define MAX_FACETS 32
#define MAX_INCIDENCES 64
#define MAX_VERTICES 128

typedef struct build_s
{
	sg_rune_cx_cell_t cells[MAX_CELLS];
	uint32_t cell_count;
	sg_rune_cx_facet_t facets[MAX_FACETS];
	uint32_t facet_count;
	sg_rune_cx_incidence_t incidences[MAX_INCIDENCES];
	uint32_t incidence_count;
	uint32_t cell_incidences[MAX_INCIDENCES];
	uint32_t cell_incidence_count;
	sg_rune_cx_vec3_t vertices[MAX_VERTICES];
	uint32_t vertex_count;
	sg_rune_cx_portal_t portals[MAX_FACETS];
	uint32_t portal_count;
} build_t;

static build_t build;

static void Q8(sg_rune_cx_vec3_t *v, float x, float y, float z)
{
	v->value[0] = (int32_t)lrintf(x * 8.0f);
	v->value[1] = (int32_t)lrintf(y * 8.0f);
	v->value[2] = (int32_t)lrintf(z * 8.0f);
}

static uint32_t Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

/* An axis-aligned box cell; its six facets are added by AddFacets. */
static uint32_t AddCell(float x0, float y0, float z0, float x1, float y1,
	float z1, uint32_t semantics)
{
	sg_rune_cx_cell_t *cell = &build.cells[build.cell_count];

	memset(cell, 0, sizeof(*cell));
	Q8(&cell->bounds.mins, x0, y0, z0);
	Q8(&cell->bounds.maxs, x1, y1, z1);
	cell->valid_stances = SG_RUNE_CX_STANCE_ALL;
	cell->semantics = semantics;
	return build.cell_count++;
}

/* A facet with plane n.p = d bounding cell on the given side; when other
 * is not NONE the facet is shared with that cell on the opposite side and
 * two directed portals are made. */
static uint32_t AddFacet(uint32_t cell, uint32_t side, const float normal[3],
	float distance, uint32_t other, float quad[4][3])
{
	sg_rune_cx_facet_t *facet = &build.facets[build.facet_count];
	uint32_t facet_index = build.facet_count++;
	uint32_t index;

	memset(facet, 0, sizeof(*facet));
	facet->plane.normal_bits[0] = Bits(normal[0]);
	facet->plane.normal_bits[1] = Bits(normal[1]);
	facet->plane.normal_bits[2] = Bits(normal[2]);
	facet->plane.distance_bits = Bits(distance);
	facet->vertices.first = build.vertex_count;
	facet->vertices.count = 4U;
	for (index = 0U; index < 4U; index++)
		Q8(&build.vertices[build.vertex_count++], quad[index][0],
			quad[index][1], quad[index][2]);
	facet->incidences.first = build.incidence_count;
	facet->portal = SG_RUNE_CX_INDEX_NONE;
	build.incidences[build.incidence_count].cell = cell;
	build.incidences[build.incidence_count].facet = facet_index;
	build.incidences[build.incidence_count].side = side;
	build.incidence_count++;
	facet->incidences.count = 1U;
	if (other != SG_RUNE_CX_INDEX_NONE)
	{
		uint32_t a = build.incidence_count - 1U, b = build.incidence_count;
		sg_rune_cx_portal_t *forward = &build.portals[build.portal_count];
		sg_rune_cx_portal_t *reverse = &build.portals[build.portal_count + 1U];

		build.incidences[b].cell = other;
		build.incidences[b].facet = facet_index;
		build.incidences[b].side = side == SG_RUNE_CX_NEGATIVE_SIDE ?
			SG_RUNE_CX_POSITIVE_SIDE : SG_RUNE_CX_NEGATIVE_SIDE;
		build.incidence_count++;
		facet->incidences.count = 2U;
		memset(forward, 0, sizeof(*forward));
		forward->facet = facet_index;
		forward->source_incidence = a;
		forward->destination_incidence = b;
		forward->valid_stances = SG_RUNE_CX_STANCE_ALL;
		forward->clearance_q8 = 56U * 8U;
		*reverse = *forward;
		reverse->source_incidence = b;
		reverse->destination_incidence = a;
		facet->portal = build.portal_count;
		build.portal_count += 2U;
	}
	return facet_index;
}

static void Quad(float q[4][3], float ax, float ay, float az, float bx,
	float by, float bz, float cx, float cy, float cz, float dx, float dy,
	float dz)
{
	q[0][0] = ax; q[0][1] = ay; q[0][2] = az;
	q[1][0] = bx; q[1][1] = by; q[1][2] = bz;
	q[2][0] = cx; q[2][1] = cy; q[2][2] = cz;
	q[3][0] = dx; q[3][1] = dy; q[3][2] = dz;
}

/* Closed box faces for a cell on all six sides except those listed as
 * shared (given by axis and direction with the neighbour). */
static void BoxFaces(uint32_t cell, uint32_t px_neighbour, uint32_t nx_neighbour,
	uint32_t pz_neighbour, uint32_t nz_neighbour)
{
	const sg_rune_cx_cell_t *c = &build.cells[cell];
	float x0 = (float)c->bounds.mins.value[0] / 8.0f, x1 = (float)c->bounds.maxs.value[0] / 8.0f;
	float y0 = (float)c->bounds.mins.value[1] / 8.0f, y1 = (float)c->bounds.maxs.value[1] / 8.0f;
	float z0 = (float)c->bounds.mins.value[2] / 8.0f, z1 = (float)c->bounds.maxs.value[2] / 8.0f;
	float n[3], q[4][3];

	n[0] = 1.0f; n[1] = 0.0f; n[2] = 0.0f;
	Quad(q, x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1);
	AddFacet(cell, SG_RUNE_CX_NEGATIVE_SIDE, n, x1, px_neighbour, q);
	if (nx_neighbour == SG_RUNE_CX_INDEX_NONE)
	{
		n[0] = -1.0f;
		Quad(q, x0, y0, z0, x0, y1, z0, x0, y1, z1, x0, y0, z1);
		AddFacet(cell, SG_RUNE_CX_NEGATIVE_SIDE, n, -x0, SG_RUNE_CX_INDEX_NONE, q);
	}
	n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f;
	Quad(q, x0, y1, z0, x1, y1, z0, x1, y1, z1, x0, y1, z1);
	AddFacet(cell, SG_RUNE_CX_NEGATIVE_SIDE, n, y1, SG_RUNE_CX_INDEX_NONE, q);
	n[1] = -1.0f;
	Quad(q, x0, y0, z0, x1, y0, z0, x1, y0, z1, x0, y0, z1);
	AddFacet(cell, SG_RUNE_CX_NEGATIVE_SIDE, n, -y0, SG_RUNE_CX_INDEX_NONE, q);
	n[1] = 0.0f; n[2] = 1.0f;
	Quad(q, x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1);
	AddFacet(cell, SG_RUNE_CX_NEGATIVE_SIDE, n, z1, pz_neighbour, q);
	if (nz_neighbour == SG_RUNE_CX_INDEX_NONE)
	{
		n[2] = -1.0f;
		Quad(q, x0, y0, z0, x1, y0, z0, x1, y1, z0, x0, y1, z0);
		AddFacet(cell, SG_RUNE_CX_NEGATIVE_SIDE, n, -z0, SG_RUNE_CX_INDEX_NONE, q);
	}
}

static void CellIncidences(void)
{
	uint32_t cell, index;

	build.cell_incidence_count = 0U;
	for (cell = 0U; cell < build.cell_count; cell++)
	{
		build.cells[cell].incidences.first = build.cell_incidence_count;
		for (index = 0U; index < build.incidence_count; index++)
			if (build.incidences[index].cell == cell)
				build.cell_incidences[build.cell_incidence_count++] = index;
		build.cells[cell].incidences.count =
			build.cell_incidence_count - build.cells[cell].incidences.first;
	}
}

int main(void)
{
	sg_rune_artifact_t artifact;
	sg_rune_move_store_t store;
	sg_rune_move_law_t movement_law = { 800.0f, 100U, 25U };
	sg_rune_locator_t locator;
	sg_rune_router_t router;
	sg_rune_field_t field;
	sg_rune_step_t step;
	sg_rune_flight_t flight;
	sg_rune_mech_t mech;
	uint32_t a, b, c, air, low, cell;
	float point[3], violation;
	const float NONE = 0.0f;

	(void)NONE;
	memset(&build, 0, sizeof(build));
	/* Three floor cells at origin height z 24..88 (floor at 0, origins from
	 * 24), then an air column x 192..320 down to a lower floor at -64. */
	a = AddCell(0.0f, -32.0f, 24.0f, 64.0f, 32.0f, 88.0f, SG_RUNE_CX_CELL_SUPPORTED);
	b = AddCell(64.0f, -32.0f, 24.0f, 128.0f, 32.0f, 88.0f, SG_RUNE_CX_CELL_SUPPORTED);
	c = AddCell(128.0f, -32.0f, 24.0f, 192.0f, 32.0f, 88.0f, SG_RUNE_CX_CELL_SUPPORTED);
	low = AddCell(192.0f, -32.0f, -40.0f, 320.0f, 32.0f, 24.0f, SG_RUNE_CX_CELL_SUPPORTED);
	air = AddCell(192.0f, -32.0f, 24.0f, 320.0f, 32.0f, 88.0f, 0U);
	BoxFaces(a, b, SG_RUNE_CX_INDEX_NONE, SG_RUNE_CX_INDEX_NONE, SG_RUNE_CX_INDEX_NONE);
	BoxFaces(b, c, a, SG_RUNE_CX_INDEX_NONE, SG_RUNE_CX_INDEX_NONE);
	BoxFaces(c, air, b, SG_RUNE_CX_INDEX_NONE, SG_RUNE_CX_INDEX_NONE);
	BoxFaces(low, SG_RUNE_CX_INDEX_NONE, SG_RUNE_CX_INDEX_NONE, air, SG_RUNE_CX_INDEX_NONE);
	BoxFaces(air, SG_RUNE_CX_INDEX_NONE, c, SG_RUNE_CX_INDEX_NONE, low);
	CellIncidences();

	memset(&artifact, 0, sizeof(artifact));
	artifact.identity.schema_id = SG_RUNE_ARTIFACT_SCHEMA_ID;
	artifact.law.gravity = 800.0f;
	artifact.law.frame_ms = 100U;
	artifact.law.substep_ms = 25U;
	artifact.law.max_velocity = 2000.0f;
	artifact.law.standing_mins[0] = artifact.law.standing_mins[1] = -16.0f;
	artifact.law.standing_mins[2] = -24.0f;
	artifact.law.standing_maxs[0] = artifact.law.standing_maxs[1] = 16.0f;
	artifact.law.standing_maxs[2] = 32.0f;
	artifact.law.crouching_mins[0] = artifact.law.crouching_mins[1] = -16.0f;
	artifact.law.crouching_mins[2] = -24.0f;
	artifact.law.crouching_maxs[0] = artifact.law.crouching_maxs[1] = 16.0f;
	artifact.law.crouching_maxs[2] = 4.0f;
	artifact.complex.cells = build.cells;
	artifact.complex.cell_count = build.cell_count;
	artifact.complex.facets = build.facets;
	artifact.complex.facet_count = build.facet_count;
	artifact.complex.incidences = build.incidences;
	artifact.complex.incidence_count = build.incidence_count;
	artifact.complex.cell_incidences = build.cell_incidences;
	artifact.complex.cell_incidence_count = build.cell_incidence_count;
	artifact.complex.vertices = build.vertices;
	artifact.complex.vertex_count = build.vertex_count;
	artifact.complex.portals = build.portals;
	artifact.complex.portal_count = build.portal_count;
	CHECK(SG_RuneCxViewValid(&artifact.complex, NULL));

	/* Movement from the complex: walks between the floor cells, a drop and
	 * a jump off c into the air that land in the lower floor. */
	CHECK(SG_RuneMoveStoreInit(&store, &movement_law));
	CHECK(SG_RuneMoveEmitComplex(&store, &artifact.complex, &artifact.law));
	/* A mechanism crossing has no portal: a teleporter from low back to a. */
	{
		memset(&mech, 0, sizeof(mech));
		mech.kind = SG_RUNE_MECH_TELEPORTER;
		mech.activator = SG_RUNE_CX_INDEX_NONE;
		mech.bmodel = -1;
		artifact.mechanisms.records = &mech;
		artifact.mechanisms.record_count = 1U;
		CHECK(SG_RuneMoveAppendMechanism(&store, low, a, SG_RUNE_MOVE_TELEPORT,
			SG_RUNE_MOVE_STANDING | SG_RUNE_MOVE_CROUCHING, 0U, NULL, 0.5f));
	}
	SG_RuneMoveStoreView(&store, &artifact.movement);
	CHECK(SG_RuneArtifactValid(&artifact, NULL));
	{
		uint32_t index, walks = 0U, drops = 0U, jumps = 0U;

		for (index = 0U; index < artifact.movement.capability_count; index++)
		{
			const sg_rune_move_capability_t *record =
				&artifact.movement.capabilities[index];

			if (record->kind == SG_RUNE_MOVE_WALK)
				walks++;
			if (record->kind == SG_RUNE_MOVE_DROP)
			{
				drops++;
				CHECK(record->cell == c && record->destination == low);
				CHECK(record->seconds > 0.3f && record->seconds < 1.0f);
			}
			if (record->kind == SG_RUNE_MOVE_JUMP && record->cell == c)
			{
				jumps++;
				CHECK(record->destination == low);
			}
		}
		CHECK(walks == 4U);
		CHECK(drops == 1U);
		CHECK(jumps == 1U);
	}

	/* Flight: from the edge of c at full speed, no impulse, lands low. */
	point[0] = 192.0f; point[1] = 0.0f; point[2] = 24.0f;
	{
		const float velocity[3] = { 300.0f, 0.0f, 0.0f };

		CHECK(SG_RuneFlightTrace(&artifact.complex, &artifact.law, c, point,
			velocity, &flight));
		CHECK(flight.outcome == SG_RUNE_FLIGHT_LANDED);
		CHECK(flight.landing_cell == low);
		CHECK(fabsf(flight.landing[2] - (-40.0f)) < 0.5f);
		CHECK(flight.seconds > 0.35f && flight.seconds < 0.45f);
	}

	/* Locate. */
	CHECK(SG_RuneLocatorBuild(&locator, &artifact));
	point[0] = 30.0f; point[1] = 0.0f; point[2] = 40.0f;
	CHECK(SG_RuneLocate(&locator, point, SG_RUNE_MOVE_STANDING, 0.0f,
		&violation) == a);
	CHECK(violation == 0.0f);
	point[0] = 250.0f; point[2] = 50.0f;
	CHECK(SG_RuneLocate(&locator, point, SG_RUNE_MOVE_STANDING, 0.0f,
		&violation) == air);
	point[2] = 24.0f;   /* the shared boundary: the supported cell wins */
	CHECK(SG_RuneLocate(&locator, point, SG_RUNE_MOVE_STANDING, 0.0f,
		&violation) == low);
	point[0] = 1000.0f;
	CHECK(SG_RuneLocate(&locator, point, SG_RUNE_MOVE_STANDING, 8.0f,
		&violation) == SG_RUNE_CX_INDEX_NONE);

	/* Field toward low from a: walk, walk, then drop (cheaper than the
	 * jump), arriving in low. */
	CHECK(SG_RuneRouterBuild(&router, &artifact));
	memset(&field, 0, sizeof(field));
	CHECK(SG_RuneFieldBuild(&field, &router, low));
	CHECK(field.cost[SG_RUNE_FIELD_STATE(a, 0)] < INFINITY);
	CHECK(field.cost[SG_RUNE_FIELD_STATE(air, 0)] == INFINITY);
	cell = a;
	{
		uint32_t hops = 0U;
		uint8_t kinds[8] = { 0 };

		point[0] = 256.0f; point[1] = 0.0f; point[2] = -40.0f;
		while (hops < 8U)
		{
			CHECK(SG_RuneStepSelect(&router, &field, cell, 0, point, &step));
			if (step.kind != SG_RUNE_STEP_CROSS)
				break;
			kinds[hops] = step.move_kind;
			cell = router.destination[step.capability];
			hops++;
		}
		CHECK(step.kind == SG_RUNE_STEP_ARRIVED);
		CHECK(hops == 3U);
		CHECK(kinds[0] == SG_RUNE_MOVE_WALK && kinds[1] == SG_RUNE_MOVE_WALK);
		CHECK(kinds[2] == SG_RUNE_MOVE_DROP);
		CHECK(fabsf(step.target[0] - 256.0f) < 0.01f);
	}
	/* From low, only the teleporter leads back up.  The field's arrays are
	 * reused for the new destination. */
	CHECK(SG_RuneFieldBuild(&field, &router, a));
	CHECK(SG_RuneStepSelect(&router, &field, low, 0, point, &step));
	CHECK(step.kind == SG_RUNE_STEP_CROSS);
	CHECK(step.move_kind == SG_RUNE_MOVE_TELEPORT);
	CHECK(step.portal == SG_RUNE_CX_INDEX_NONE);
	CHECK(fabsf(step.target[0] - 32.0f) < 0.01f);

	SG_RuneFieldFree(&field);
	SG_RuneRouterFree(&router);
	SG_RuneLocatorFree(&locator);
	SG_RuneMoveStoreFree(&store);
	if (failures)
	{
		fprintf(stderr, "sg_rune_runtime_test: %d failures\n", failures);
		return 1;
	}
	printf("sg_rune_runtime_test: ok\n");
	return 0;
}
