#include "sg_destination.h"

static int TerminalDomainValid(const sg_destination_terminal_domain_t *domain)
{
	return domain && domain->chart_identity != 0U &&
		domain->domain_identity != 0U;
}

int SG_DestinationTerminalValid(const sg_destination_terminal_t *terminal)
{
	size_t index;

	if (!terminal || !SG_DestinationRefValid(&terminal->destination) ||
	    terminal->generation == 0U ||
	    terminal->kind < SG_DESTINATION_TERMINAL_STATIC_PATCH ||
	    terminal->kind >= SG_DESTINATION_TERMINAL_KIND_COUNT)
		return 0;

	if (terminal->kind == SG_DESTINATION_TERMINAL_STATIC_PATCH)
		return TerminalDomainValid(&terminal->value.static_patch.domain);

	if (terminal->value.moving_tube.trajectory_identity == 0U ||
	    !terminal->value.moving_tube.segments ||
	    terminal->value.moving_tube.segment_count == 0U)
		return 0;
	for (index = 0U; index < terminal->value.moving_tube.segment_count; index++)
	{
		const sg_destination_tube_segment_t *segment =
			&terminal->value.moving_tube.segments[index];
		if (segment->valid_from_ms >= segment->valid_until_ms ||
		    !TerminalDomainValid(&segment->domain) ||
		    (index != 0U &&
		     terminal->value.moving_tube.segments[index - 1U].valid_until_ms >
			segment->valid_from_ms))
			return 0;
	}
	return 1;
}
