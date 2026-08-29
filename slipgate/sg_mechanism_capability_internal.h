#ifndef SG_MECHANISM_CAPABILITY_INTERNAL_H
#define SG_MECHANISM_CAPABILITY_INTERNAL_H

#include "sg_mechanism_capability.h"

/* Only the producer may issue the keyed downstream acceptance stamp. */
void SG_MechanismCapabilitySetStamp(
	sg_mechanism_capability_set_t *capabilities);

#endif /* SG_MECHANISM_CAPABILITY_INTERNAL_H */
