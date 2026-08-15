/* Focused contract test for candidate DPO root selection. */
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"

int Fields_DefensiveRoot(const rune_t *r, const unsigned char *plane);

_Static_assert(SG_DPO_POST_RED == 0, "DPO post-red plane drift");
_Static_assert(SG_DPO_POST_BLUE == 1, "DPO post-blue plane drift");
_Static_assert(SG_DPO_INTERCEPT_RED == 2, "DPO intercept-red plane drift");
_Static_assert(SG_DPO_INTERCEPT_BLUE == 3, "DPO intercept-blue plane drift");
_Static_assert(SG_DPO_PLANE_COUNT == 4, "DPO plane-count drift");

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

int main(void)
{
	rune_t rune;
	rune_seed_t seeds[5];
	byte linked[5];
	unsigned char plane[5];

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	memset(linked, 1, sizeof(linked));
	rune.hdr.num_seeds = 5;
	rune.seeds = seeds;

	CHECK(Fields_DefensiveRoot(&rune, NULL) == -1);
	memset(plane, 0, sizeof(plane));
	CHECK(Fields_DefensiveRoot(&rune, plane) == -1);

	/* The first maximum wins, preserving the historic deterministic tie. */
	plane[0] = 7;
	plane[1] = 12;
	plane[2] = 12;
	CHECK(Fields_DefensiveRoot(&rune, plane) == 1);

	/* Even a malformed nonzero tombstone cell can never become the root. */
	seeds[1].flags = RSF_TOMBSTONE;
	plane[1] = 255;
	CHECK(Fields_DefensiveRoot(&rune, plane) == 2);

	memset(plane, 0, sizeof(plane));
	plane[1] = 255;
	CHECK(Fields_DefensiveRoot(&rune, plane) == -1);

	/* A corrupted non-tombstone without outgoing ownership is also inert. */
	memset(seeds, 0, sizeof(seeds));
	memset(plane, 0, sizeof(plane));
	rune.linked_seed = linked;
	linked[3] = 0;
	plane[3] = 255;
	plane[4] = 9;
	CHECK(Fields_DefensiveRoot(&rune, plane) == 4);

	if (failures)
	{
		fprintf(stderr, "sg_fields_candidate_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_fields_candidate_test: ok");
	return 0;
}
