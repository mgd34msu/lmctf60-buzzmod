/* Loader-replayed suffix plans for dormant D_DROP and D_HOOK. */
#ifndef SG_COMPOUND_ACTION_PUBLICATION_H
#define SG_COMPOUND_ACTION_PUBLICATION_H

#include "../q_shared.h"
#include "sg_compound_publication.h"
#include "sg_replay.h"

/* Hook phase timings and the exact bite cannot be reconstructed from the
 * serialized link alone. The loader oracle publishes this pointer-free copy. */
typedef struct sg_compound_hook_publication_proof_s
{
	sg_hook_replay_spec_t spec;
} sg_compound_hook_publication_proof_t;

qboolean SG_CompoundDropPublicationPlan(
	const sg_compound_publication_binding_t *binding,
	sg_drop_replay_spec_t *spec_out);
qboolean SG_CompoundHookPublicationPlan(
	const sg_compound_publication_binding_t *binding,
	const sg_compound_hook_publication_proof_t *proof,
	sg_hook_replay_spec_t *spec_out);

#endif /* SG_COMPOUND_ACTION_PUBLICATION_H */
