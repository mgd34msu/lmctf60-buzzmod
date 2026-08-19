/* Stable unique roster identity selection over the sixteen authored rows. */
#ifndef SG_PERSONA_ASSIGNMENT_H
#define SG_PERSONA_ASSIGNMENT_H

#include <stdint.h>

static inline unsigned SG_PersonaAssignmentChoose(uint32_t occupied_mask,
	unsigned preferred_slot)
{
	unsigned offset;
	unsigned preferred = preferred_slot & 15u;

	for (offset = 0; offset < 16u; offset++)
	{
		unsigned candidate = (preferred + offset) & 15u;

		if ((occupied_mask & (UINT32_C(1) << candidate)) == 0)
			return candidate;
	}
	return preferred;
}

#endif /* SG_PERSONA_ASSIGNMENT_H */
