/* Regression tests for the legacy/v3 nominal-gravity scope boundary. */
#include <math.h>
#include <stdio.h>

#include "slipgate/sg_rune_proof.h"

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
	/* The v2 gate historically casts to short before comparing with 800.
	 * B2 must not tighten that frozen compatibility boundary. */
	CHECK(SG_RuneV2GravityCompatible(800.0f));
	CHECK(SG_RuneV2GravityCompatible(800.5f));
	CHECK(!SG_RuneV2GravityCompatible(799.5f));
	CHECK(!SG_RuneV2GravityCompatible(INFINITY));
	{
		float off = 0.0f;
		float on = 1.0f;
		float invalid = NAN;

		CHECK(SG_RuneV3FunkyGravityCompatible(&off));
		CHECK(!SG_RuneV3FunkyGravityCompatible(&on));
		CHECK(!SG_RuneV3FunkyGravityCompatible(&invalid));
		CHECK(!SG_RuneV3FunkyGravityCompatible(NULL));
	}

	SG_RuneProofScopeEnd();
	CHECK(!SG_RuneProofScopeActive());
	CHECK(SG_RuneProofGravity() == 800);

	CHECK(!SG_RuneProofScopeBegin(650.5f));
	CHECK(!SG_RuneProofScopeActive());
	CHECK(SG_RuneProofGravity() == 800);

	CHECK(SG_RuneProofScopeBegin(650.0f));
	CHECK(SG_RuneProofScopeActive());
	CHECK(SG_RuneProofGravity() == 650);
	CHECK(!SG_RuneProofScopeBegin(800.0f));
	CHECK(SG_RuneProofGravity() == 650);

	/* Simulate every post-begin failure funnel: End is idempotent and the next
	 * invocation starts from the legacy default, never the prior map's law. */
	SG_RuneProofScopeEnd();
	SG_RuneProofScopeEnd();
	CHECK(!SG_RuneProofScopeActive());
	CHECK(SG_RuneProofGravity() == 800);
	CHECK(SG_RuneProofScopeBegin(800.0f));
	CHECK(SG_RuneProofGravity() == 800);
	SG_RuneProofScopeEnd();
	CHECK(!SG_RuneProofScopeActive());
	CHECK(SG_RuneProofGravity() == 800);

	if (failures)
	{
		fprintf(stderr, "sg_rune_proof_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_rune_proof_test: ok");
	return 0;
}
