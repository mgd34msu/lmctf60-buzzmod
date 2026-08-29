#include "sg_destination.h"


static int TerminalDomainValid(const sg_destination_terminal_domain_t *domain)
{
	return domain && SG_RuneModelStableIdValid(&domain->chart.value) &&
		(uint32_t)(domain->chart.value.high >> 32) ==
			SG_RUNE_ORDER_STATE_CHART &&
		SG_RuneModelStableIdValid(&domain->domain.value) &&
		(uint32_t)(domain->domain.value.high >> 32) ==
			SG_RUNE_ORDER_STATE_DOMAIN &&
		domain->chart.value.source_set_identity ==
			domain->domain.value.source_set_identity;
}

static int TerminalIntervalValid(const sg_destination_interval_t *interval)
{
	return interval && SG_DestinationFloatValid(interval->min_value) &&
		SG_DestinationFloatValid(interval->max_value) &&
		interval->min_value <= interval->max_value;
}

static int TerminalInterval3Valid(const sg_destination_interval3_t *interval)
{
	return interval && TerminalIntervalValid(&interval->x) &&
		TerminalIntervalValid(&interval->y) &&
		TerminalIntervalValid(&interval->z);
}

static int DestinationRefSame(const sg_destination_ref_t *left,
	const sg_destination_ref_t *right)
{
	if (!left || !right || left->kind != right->kind)
		return 0;
	switch (left->kind)
	{
	case SG_DESTINATION_FLAG:
		return left->value.flag.team == right->value.flag.team &&
			left->value.flag.location == right->value.flag.location &&
			left->value.flag.reserved == right->value.flag.reserved;
	case SG_DESTINATION_ITEM:
	case SG_DESTINATION_WEAPON:
	case SG_DESTINATION_ARMOR:
	case SG_DESTINATION_POWERUP:
		return left->value.item.item_id == right->value.item.item_id;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
		return left->value.carrier.client_id == right->value.carrier.client_id &&
			left->value.carrier.team == right->value.carrier.team &&
			left->value.carrier.selector == right->value.carrier.selector;
	case SG_DESTINATION_DEFENSIVE_POST:
		return left->value.post.region_id == right->value.post.region_id;
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
		return left->value.point.point_id == right->value.point.point_id;
	case SG_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

int SG_DestinationTerminalCaptureValidFor(
	const sg_destination_terminal_capture_t *capture,
	const sg_destination_ref_t *destination, uint64_t generation)
{
	uint32_t axis;

	if (!capture || capture->anchor.owner_identity == 0U ||
	    !DestinationRefSame(&capture->anchor.destination, destination) ||
	    capture->anchor.destination_generation != generation ||
	    !SG_DestinationFloatValid(capture->anchor.local_elapsed_ms) ||
	    capture->anchor.local_elapsed_ms < 0.0f ||
	    !TerminalInterval3Valid(&capture->position_offset) ||
	    !TerminalInterval3Valid(&capture->velocity) ||
	    !TerminalIntervalValid(&capture->local_elapsed_ms) ||
	    capture->local_elapsed_ms.min_value < 0.0f)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_DestinationFloatValid(capture->anchor.position[axis]) ||
		    !SG_DestinationFloatValid(capture->anchor.velocity[axis]))
			return 0;
	return 1;
}

int SG_DestinationTerminalValid(const sg_destination_terminal_t *terminal)
{
	size_t index;

	if (!terminal || terminal->owner_identity == 0U ||
	    !SG_DestinationRefValid(&terminal->destination) ||
	    terminal->generation == 0U ||
	    terminal->kind < SG_DESTINATION_TERMINAL_STATIC_PATCH ||
	    terminal->kind >= SG_DESTINATION_TERMINAL_KIND_COUNT)
		return 0;

	if (terminal->kind == SG_DESTINATION_TERMINAL_STATIC_PATCH)
		return TerminalDomainValid(&terminal->value.static_patch.domain) &&
			terminal->value.static_patch.capture.anchor.owner_identity ==
				terminal->owner_identity &&
			SG_DestinationTerminalCaptureValidFor(
				&terminal->value.static_patch.capture,
				&terminal->destination, terminal->generation);

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
		    segment->capture.anchor.owner_identity !=
			terminal->owner_identity ||
		    !SG_DestinationTerminalCaptureValidFor(&segment->capture,
			&terminal->destination,
			terminal->generation) ||
		    (index != 0U &&
		     terminal->value.moving_tube.segments[index - 1U].valid_until_ms >
			segment->valid_from_ms))
			return 0;
	}
	return 1;
}
