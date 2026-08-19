/* sg_rune_proof.c -- scoped nominal gravity for RUNE generation. */
#include <math.h>

/* q_shared.h intentionally has no include guard and must precede sg_rune.h. */
#include "q_shared.h"
#include "slipgate/sg_rune.h"
#include "slipgate/sg_rune_proof.h"

static short sg_rune_scoped_gravity = (short)RUNE_PROOF_GRAVITY;
static int sg_rune_proof_scope_active;

int SG_RuneFunkyGravityCompatible(const float *value)
{
	return value && isfinite(*value) &&
	       *value == (float)SG_RUNE_PROOF_FUNKY_GRAVITY_REQUIRED;
}

int SG_RuneProofHookLateralWindow(float horizontal, float rise)
{
	return isfinite(horizontal) && isfinite(rise) &&
	       horizontal >= 0.0f &&
	       horizontal <= SG_RUNE_PROOF_HOOK_LATERAL_MAX_HORIZONTAL &&
	       rise >= SG_RUNE_PROOF_HOOK_LATERAL_MIN_RISE &&
	       rise <= SG_RUNE_PROOF_HOOK_LATERAL_MAX_RISE;
}

int SG_RuneProofScopeBegin(float gravity)
{
	if (sg_rune_proof_scope_active || !isfinite(gravity) ||
	    gravity < (float)SG_RUNE_PROOF_GRAVITY_MIN ||
	    gravity > (float)SG_RUNE_PROOF_GRAVITY_MAX ||
	    (SG_RUNE_PROOF_GRAVITY_INTEGRAL_REQUIRED &&
	     gravity != (float)(short)gravity))
		return 0;
	sg_rune_scoped_gravity = (short)gravity;
	sg_rune_proof_scope_active = 1;
	return 1;
}

void SG_RuneProofScopeEnd(void)
{
	sg_rune_scoped_gravity = (short)RUNE_PROOF_GRAVITY;
	sg_rune_proof_scope_active = 0;
}

short SG_RuneProofGravity(void)
{
	return sg_rune_proof_scope_active
		? sg_rune_scoped_gravity : (short)RUNE_PROOF_GRAVITY;
}

int SG_RuneProofScopeActive(void)
{
	return sg_rune_proof_scope_active;
}
