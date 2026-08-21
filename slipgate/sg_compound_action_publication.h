/* Loader-replayed suffix plans for compound door links. */
#ifndef SG_COMPOUND_ACTION_PUBLICATION_H
#define SG_COMPOUND_ACTION_PUBLICATION_H

#include "sg_compound_publication.h"

qboolean SG_CompoundDropPublicationPlan(
	const sg_compound_publication_binding_t *binding,
	sg_drop_replay_spec_t *spec_out);
qboolean SG_CompoundHookPublicationPlan(
	const sg_compound_publication_binding_t *binding,
	sg_hook_replay_spec_t *spec_out);

#endif /* SG_COMPOUND_ACTION_PUBLICATION_H */
