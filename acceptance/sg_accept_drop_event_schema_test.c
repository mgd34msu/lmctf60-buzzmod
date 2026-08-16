/* Capture and parse the bytes emitted by the real private DROP formatters. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CAPTURE_RECORDS 4096
#define CAPTURE_BYTES 4096

static char captured[CAPTURE_RECORDS][CAPTURE_BYTES];
static size_t captured_lengths[CAPTURE_RECORDS];
static size_t captured_count;
static size_t captured_partial;
static int captured_overflow;

static void AcceptEventCaptureV(const char *format, va_list arguments)
{
	char fragment[CAPTURE_BYTES];
	va_list copy;
	int length;
	int index;

	va_copy(copy, arguments);
	length = vsnprintf(fragment, sizeof(fragment), format, copy);
	va_end(copy);
	if (length < 0 || (size_t)length >= sizeof(fragment))
	{
		captured_overflow = 1;
		return;
	}
	for (index = 0; index < length; index++)
	{
		if (captured_count >= CAPTURE_RECORDS ||
		    captured_partial + 1 >= CAPTURE_BYTES)
		{
			captured_overflow = 1;
			return;
		}
		captured[captured_count][captured_partial++] = fragment[index];
		if (fragment[index] == '\n')
		{
			captured[captured_count][captured_partial] = '\0';
			captured_lengths[captured_count] = captured_partial;
			captured_count++;
			captured_partial = 0;
		}
	}
}

#define SG_ACCEPT_DROP_CAPTURE_DPRINT 1
#define main AcceptInjectionScenariosMain
#include "sg_accept_drop_injection_test.c"
#undef main

#define KEY_COUNT 160
#define KEY_BYTES 80

static int RecordHasUniqueKeys(const char *record)
{
	char copy[CAPTURE_BYTES];
	char keys[KEY_COUNT][KEY_BYTES];
	char *token;
	int count = 0;
	int index;
	const unsigned char *byte;

	if (!record || strlen(record) >= sizeof(copy))
		return 0;
	if (record[0] == ' ' || strstr(record, "  ") || strstr(record, " \n"))
		return 0;
	for (byte = (const unsigned char *)record; *byte; byte++)
		if (*byte != ' ' && *byte != '\n' && (*byte < 0x21 || *byte > 0x7e))
			return 0;
	snprintf(copy, sizeof(copy), "%s", record);
	token = strtok(copy, " \n");
	if (!token || strcmp(token, "SG_ACCEPT_DROP") != 0)
		return 0;
	while ((token = strtok(NULL, " \n")) != NULL)
	{
		char *equals = strchr(token, '=');
		size_t length;

		if (!equals || equals == token || !equals[1] || strchr(equals + 1, '='))
			return 0;
		length = (size_t)(equals - token);
		if (length >= KEY_BYTES || count >= KEY_COUNT)
			return 0;
		for (index = 0; index < count; index++)
			if (strlen(keys[index]) == length &&
			    memcmp(keys[index], token, length) == 0)
				return 0;
		memcpy(keys[count], token, length);
		keys[count][length] = '\0';
		count++;
	}
	return count >= 6;
}

static const char *RecordEvent(const char *record, char *event, size_t capacity)
{
	const char *start = strstr(record, " event=");
	const char *end;
	size_t length;

	if (!start)
		return NULL;
	start += strlen(" event=");
	end = strpbrk(start, " \n");
	if (!end)
		return NULL;
	length = (size_t)(end - start);
	if (length == 0 || length >= capacity)
		return NULL;
	memcpy(event, start, length);
	event[length] = '\0';
	return event;
}

static int CapturedEvent(const char *wanted)
{
	char event[KEY_BYTES];
	size_t index;

	for (index = 0; index < captured_count; index++)
		if (RecordEvent(captured[index], event, sizeof(event)) &&
		    strcmp(event, wanted) == 0)
			return 1;
	return 0;
}

static int EmitAdditionalRealPaths(void)
{
	const sg_accept_drop_selector_t *selector = &accept_selectors[3];
	sg_bot_t bot;
	edict_t ent;
	gclient_t client;
	rune_link_t *link;

	SG_AcceptDropFrameEvent("boundary");
	SetupLateCadence(selector, 1, true, true, false, &bot, &ent, &client);
	PrepareBoundaryFixture(selector, &ent, &client);
	link = &boundary_links[selector->expected_link];
	link->from = selector->source;
	link->to = selector->destination;
	link->cost_ms = selector->cost_ms;
	link->heading = selector->heading;
	link->anchor[0] = BitsFloat(selector->anchor_bits[0]);
	link->anchor[1] = BitsFloat(selector->anchor_bits[1]);
	link->anchor[2] = BitsFloat(selector->anchor_bits[2]);
	accept_drop.started = false;
	SG_AcceptDropPose(&bot, selector->expected_link, 0, &ent);
	accept_drop.action_begins = 0;
	SG_AcceptDropActionBegin(&bot, selector->expected_link, "capture-host");
	SG_AcceptDropBoundary(&bot, selector->expected_link, NULL, NULL, NULL);
	SG_AcceptDropTeach(selector->expected_link, "short landing");
	AcceptLogSummaryFormatFailed(0);
	return 1;
}

static int EmitPublicArmPath(void)
{
	static const char map_name[SG_RUNE_V3_MAP_NAME_BYTES] = "lmctf14";
	const sg_accept_drop_selector_t *selector = &accept_selectors[3];
	rune_link_t *link;
	char link_text[32];
	int axis;

	memset(&boundary_rune, 0, sizeof(boundary_rune));
	memset(boundary_links, 0, sizeof(boundary_links));
	memset(boundary_seeds, 0, sizeof(boundary_seeds));
	memset(boundary_edicts, 0, sizeof(boundary_edicts));
	memset(&arm_client, 0, sizeof(arm_client));
	memset(sg_bots, 0, sizeof(sg_bots));
	boundary_rune.seeds = boundary_seeds;
	boundary_rune.links = boundary_links;
	boundary_rune.hdr.magic = (int)SG_RUNE_V3_MAGIC;
	boundary_rune.hdr.version = SG_RUNE_V3_VERSION;
	boundary_rune.hdr.num_seeds = 960;
	boundary_rune.hdr.num_links = 24451;
	memcpy(boundary_rune.hdr.mapname, map_name,
	    sizeof(boundary_rune.hdr.mapname));
	boundary_rune.v3_header.magic = SG_RUNE_V3_MAGIC;
	boundary_rune.v3_header.version = SG_RUNE_V3_VERSION;
	boundary_rune.v3_header.header_bytes = SG_RUNE_V3_HEADER_BYTES;
	boundary_rune.v3_header.seed_bytes = SG_RUNE_V3_SEED_BYTES;
	boundary_rune.v3_header.link_bytes = SG_RUNE_V3_LINK_BYTES;
	boundary_rune.v3_header.num_seeds = 960;
	boundary_rune.v3_header.num_links = 24451;
	boundary_rune.v3_header.payload_crc32 = UINT32_C(0xd3d0ca2f);
	boundary_rune.v3_header.bsp_checksum = UINT32_C(0x0e7c5adf);
	boundary_rune.v3_header.entity_crc32 = UINT32_C(0xbdb3e621);
	boundary_rune.v3_header.action_contract_crc32 = UINT32_C(0x5c64bc3b);
	boundary_rune.v3_header.gravity = 800.0f;
	boundary_rune.v3_header.airaccelerate = 0.0f;
	boundary_rune.v3_header.maxvelocity = 2000.0f;
	boundary_rune.v3_header.pmove_substep_ms = 25;
	boundary_rune.v3_header.server_frame_ms = 100;
	boundary_rune.v3_header.host_physics_id = 1;
	boundary_rune.v3_header.header_crc32 = UINT32_C(0x1d5e73c6);
	memcpy(boundary_rune.v3_header.map_name, map_name, sizeof(map_name));
	link = &boundary_links[selector->expected_link];
	link->from = selector->source;
	link->to = selector->destination;
	link->action = selector->action;
	link->provenance = selector->provenance;
	link->min_speed = selector->min_speed;
	link->heading = selector->heading;
	link->heading_slack = selector->heading_slack;
	link->exit_speed = selector->exit_speed;
	link->cost_ms = selector->cost_ms;
	boundary_seeds[selector->source].area_hint = selector->source_area_hint;
	boundary_seeds[selector->source].flags = selector->source_flags;
	boundary_seeds[selector->destination].area_hint =
	    selector->destination_area_hint;
	boundary_seeds[selector->destination].flags = selector->destination_flags;
	boundary_seeds[selector->fixture_seed].area_hint =
	    selector->fixture_area_hint;
	boundary_seeds[selector->fixture_seed].flags = selector->fixture_flags;
	for (axis = 0; axis < 3; axis++)
	{
		link->anchor[axis] = BitsFloat(selector->anchor_bits[axis]);
		boundary_seeds[selector->source].origin[axis] =
		    BitsFloat(selector->source_bits[axis]);
		boundary_seeds[selector->destination].origin[axis] =
		    BitsFloat(selector->destination_bits[axis]);
		boundary_seeds[selector->fixture_seed].origin[axis] =
		    BitsFloat(selector->fixture_bits[axis]);
	}
	boundary_edicts[0].inuse = true;
	boundary_edicts[0].linkcount = 41;
	boundary_edicts[1].inuse = true;
	boundary_edicts[1].client = &arm_client;
	boundary_edicts[1].flags = FL_BOT;
	boundary_edicts[1].health = 100;
	boundary_edicts[1].movetype = MOVETYPE_WALK;
	arm_client.ps.pmove.pm_type = PM_NORMAL;
	arm_gravity.value = 800.0f;
	sg_bots[0].active = true;
	sg_bots[0].ent = &boundary_edicts[1];
	sg_host.dprint = TestDprint;
	sg_host.linkentity = BoundaryLinkEntity;
	snprintf(link_text, sizeof(link_text), "%d", selector->expected_link);
	if (!SG_AcceptDropQueue(selector->name, "0", link_text))
		return 0;
	SG_AcceptDropArm();
	return accept_drop.phase == SGAD_ACTIVE && accept_drop.armed &&
	       accept_drop.bot == &sg_bots[0] &&
	       accept_drop.link == selector->expected_link;
}

static int EmitSupportedPostCommandPaths(void)
{
	static const uint32_t origin_bits[3] = {
		UINT32_C(0xc12c0000), UINT32_C(0x444fe000), UINT32_C(0xc367e000)
	};
	static const uint32_t old_origin_bits[3] = {
		UINT32_C(0xc1100000), UINT32_C(0x44500000), UINT32_C(0xc367e000)
	};
	static const uint32_t velocity_bits[3] = {
		UINT32_C(0xc2918000), UINT32_C(0xc1910000), UINT32_C(0)
	};
	const sg_accept_drop_selector_t *selector = &accept_selectors[2];
	sg_bot_t bot;
	edict_t ent;
	gclient_t client;
	int axis;

	SetupLateCadence(selector, 1, true, false, false, &bot, &ent, &client);
	PrepareBoundaryFixture(selector, &ent, &client);
	accept_drop.injection_applied = 1;
	accept_drop.injection_step = selector->injection_step;
	accept_drop.injection_fixture_seed = selector->fixture_seed;
	accept_drop.injection_frame = 100;
	accept_drop.injection_order_stage = SGAD_ORDER_INJECTED;
	level.framenum = 100;
	for (axis = 0; axis < 3; axis++)
	{
		ent.s.origin[axis] = BitsFloat(origin_bits[axis]);
		ent.s.old_origin[axis] = BitsFloat(old_origin_bits[axis]);
		ent.velocity[axis] = BitsFloat(velocity_bits[axis]);
		client.ps.pmove.origin[axis] = (short)(ent.s.origin[axis] * 8.0f);
		client.ps.pmove.velocity[axis] =
		    (short)(ent.velocity[axis] * 8.0f);
		client.oldvelocity[axis] = 0.0f;
	}
	ent.groundentity = &boundary_edicts[0];
	ent.groundentity_linkcount = boundary_edicts[0].linkcount;
	ent.watertype = 0;
	ent.waterlevel = 0;
	(void)SG_AcceptDropAfterStep(&bot, selector->expected_link,
	    selector->injection_step + 1, &ent);
	return accept_drop.injection_post_command_captures == 1 &&
	       accept_drop.post_command.captured;
}

int main(int argc, char **argv)
{
	static const char *const common_events[] = {
		"queued", "sg-runframe-begin", "entity-pass-complete", "armed",
		"pusher-begin", "pusher-end", "handoff-walkoff",
		"handoff-airborne", "action-begin",
		"command-historical", "command-final", "pose-25ms",
		"pose-arm-zero-ms", "after-step", "boundary",
		"fixture-boundary-validation", "predicate-entry",
		"predicate-result", "shelf", "teach", "private-termination",
		"injection-deferred", "injection-applied",
		"fixture-post-command-pose", "fixture-post-command-state",
		"fixture-post-command-reducer", "summary-format-failed",
		"generic-handoff", "summary-begin", "summary-command",
		"summary-contact", "summary-boundary", "summary-observer",
		"summary-observer-contact", "summary-injection-order",
		"summary-production", "summary-end"
	};
	static const char *const legacy_events[] = {
		"observer-begin", "command-observer", "observer-poststep",
		"boundary-enter-legacy", "observer-boundary", "contact-trace",
		"handoff-recovery", "private-stop-requested"
	};
	static const char *const rev2_events[] = {
		"boundary-enter-rev2", "boundary-exit-rev2", "callback-entry"
	};
	const char *const *variant_events = SG_ACCEPT_DROP_LEGACY_A ?
	    legacy_events : rev2_events;
	size_t variant_count = SG_ACCEPT_DROP_LEGACY_A ?
	    sizeof(legacy_events) / sizeof(legacy_events[0]) :
	    sizeof(rev2_events) / sizeof(rev2_events[0]);
	size_t longest = 0;
	size_t index;
	char mutation[CAPTURE_BYTES];
	char *separator;

	(void)argc;
	(void)argv;
	if (!EmitPublicArmPath() || AcceptInjectionScenariosMain() != 0 ||
	    !EmitAdditionalRealPaths() ||
	    !EmitSupportedPostCommandPaths() ||
	    captured_overflow || captured_partial != 0 || captured_count == 0)
	{
		fprintf(stderr, "actual event capture failed records=%zu tail=%zu overflow=%d\n",
		    captured_count, captured_partial, captured_overflow);
		return 1;
	}
	for (index = 0; index < captured_count; index++)
	{
		size_t length = captured_lengths[index];

		if (length == 0 || captured[index][length - 1] != '\n' ||
		    strchr(captured[index], '\n') != captured[index] + length - 1 ||
		    !RecordHasUniqueKeys(captured[index]))
		{
			fprintf(stderr, "malformed actual record index=%zu len=%zu: %s",
			    index, length, captured[index]);
			return 1;
		}
		if (length > longest)
			longest = length;
	}
	snprintf(mutation, sizeof(mutation), "%s", captured[0]);
	separator = strchr(mutation, ' ');
	if (!separator)
		return 1;
	*separator = '\t';
	if (RecordHasUniqueKeys(mutation))
		return 1;
	snprintf(mutation, sizeof(mutation), "%s", captured[0]);
	mutation[strlen(mutation) - 1] = '\r';
	if (RecordHasUniqueKeys(mutation))
		return 1;
	snprintf(mutation, sizeof(mutation), "%s", captured[0]);
	separator = strchr(mutation, '=');
	if (!separator || !separator[1])
		return 1;
	separator[1] = '=';
	if (RecordHasUniqueKeys(mutation))
		return 1;
	for (index = 0; index < sizeof(common_events) / sizeof(common_events[0]);
	     index++)
		if (!CapturedEvent(common_events[index]))
		{
			fprintf(stderr, "real formatter path not captured: %s\n",
			    common_events[index]);
			return 1;
		}
	for (index = 0; index < variant_count; index++)
		if (!CapturedEvent(variant_events[index]))
		{
			fprintf(stderr, "variant formatter path not captured: %s\n",
			    variant_events[index]);
			return 1;
		}
	printf("event-schema-selftest ok source=actual-dprint variant=%s records=%zu "
	       "events=%zu union_events=48 non_summary=39 summaries=9 "
	       "max_record=%zu partial_tail=0 overflow=0 newline=complete "
	       "grammar=ascii-space-lf exactly-one-equals unique-keys=complete\n",
	    AcceptVariant(), captured_count,
	    sizeof(common_events) / sizeof(common_events[0]) + variant_count,
	    longest);
	return 0;
}
