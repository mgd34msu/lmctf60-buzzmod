/* Regression tests for the active nominal-gravity scope boundary. */
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
	/* lmctf54's lower approach -> shelf samples rise 112 and 124 units.
	 * Admit the complete local seed tier, but not a wider or taller general
	 * hook traversal; the generator's physical prover remains authoritative. */
	CHECK(SG_RuneProofHookLateralWindow(32.0f, 112.0f));
	CHECK(SG_RuneProofHookLateralWindow(96.0f, 124.0f));
	CHECK(SG_RuneProofHookLateralWindow(128.0f, 128.0f));
	CHECK(!SG_RuneProofHookLateralWindow(128.125f, 112.0f));
	CHECK(!SG_RuneProofHookLateralWindow(32.0f, 128.125f));
	CHECK(!SG_RuneProofHookLateralWindow(32.0f, 31.875f));
	CHECK(!SG_RuneProofHookLateralWindow(NAN, 112.0f));
	CHECK(!SG_RuneProofHookLateralWindow(32.0f, NAN));

	{
		float off = 0.0f;
		float on = 1.0f;
		float invalid = NAN;

		CHECK(SG_RuneFunkyGravityCompatible(&off));
		CHECK(!SG_RuneFunkyGravityCompatible(&on));
		CHECK(!SG_RuneFunkyGravityCompatible(&invalid));
		CHECK(!SG_RuneFunkyGravityCompatible(NULL));
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
	 * invocation starts from the nominal default, never the prior map's law. */
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
