/* Private, compile-time A/B acceptance hook.  Never copy into production. */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_drop_live.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_accept_drop.h"

#include <errno.h>
#include <stdint.h>

void ClientThink(edict_t *ent, usercmd_t *command);

#ifndef SG_ACCEPT_DROP_LEGACY_A
#define SG_ACCEPT_DROP_LEGACY_A 0
#endif

typedef enum sg_accept_drop_fixture_e
{
	SGAD_FIXTURE_NONE = 0,
	SGAD_FIXTURE_DRY_SUPPORTED,
	SGAD_FIXTURE_WATER_DEPTH2
} sg_accept_drop_fixture_t;

/* A fixture injected before the final selected command must be authenticated
 * twice: once at exact zero-ms placement, then again at the next outer
 * boundary against the real command's resulting pose.  Fixtures injected
 * after command four have no selected command between placement and that
 * boundary, so their exact placement evidence remains authoritative. */
typedef enum sg_accept_drop_fixture_boundary_e
{
	SGAD_FIXTURE_BOUNDARY_NONE = 0,
	SGAD_FIXTURE_BOUNDARY_EXACT_SEED,
	SGAD_FIXTURE_BOUNDARY_POST_COMMAND
} sg_accept_drop_fixture_boundary_t;

typedef enum sg_accept_drop_checkpoint_e
{
	SGAD_CHECKPOINT_NONE = 0,
	SGAD_CHECKPOINT_LEGACY_SHORT_CONTACT,
	SGAD_CHECKPOINT_LEGACY_WET_RECOVERY,
	SGAD_CHECKPOINT_REV2_RUNNING,
	SGAD_CHECKPOINT_REV2_SHORT_LANDING
} sg_accept_drop_checkpoint_t;

typedef enum sg_accept_drop_injection_decision_e
{
	SGAD_INJECTION_INVALID = 0,
	SGAD_INJECTION_DEFER,
	SGAD_INJECTION_APPLY
} sg_accept_drop_injection_decision_t;

enum
{
	SGAD_ORDER_NONE = 0,
	SGAD_ORDER_INJECTED,
	SGAD_ORDER_ENTITY_PASS,
	SGAD_ORDER_SG_FRAME,
	SGAD_ORDER_CHECKPOINT
};

/* Keep the deferred post-command boundary fail-closed, but make a live
 * rejection self-identifying.  The only host lifecycle rollovers accepted
 * below are s.old_origin at the next G_RunFrame and oldvelocity at the prior
 * ClientEndServerFrame; every other captured field remains exact. */
typedef enum sg_accept_drop_snapshot_bit_e
{
	SGAD_SNAPSHOT_SELECTOR = 0,
	SGAD_SNAPSHOT_ENTITY,
	SGAD_SNAPSHOT_CLIENT,
	SGAD_SNAPSHOT_BOT,
	SGAD_SNAPSHOT_CAPTURED,
	SGAD_SNAPSHOT_CAPTURE_COUNT,
	SGAD_SNAPSHOT_FRAME,
	SGAD_SNAPSHOT_STEP,
	SGAD_SNAPSHOT_PMOVE_TYPE,
	SGAD_SNAPSHOT_PMOVE_GRAVITY,
	SGAD_SNAPSHOT_PMOVE_FLAGS,
	SGAD_SNAPSHOT_HEALTH,
	SGAD_SNAPSHOT_DEADFLAG,
	SGAD_SNAPSHOT_MOVETYPE,
	SGAD_SNAPSHOT_GROUNDED,
	SGAD_SNAPSHOT_GROUNDENTITY,
	SGAD_SNAPSHOT_GROUND_LINKCOUNT,
	SGAD_SNAPSHOT_SUPPORT,
	SGAD_SNAPSHOT_WATERTYPE,
	SGAD_SNAPSHOT_WATERLEVEL,
	SGAD_SNAPSHOT_HISTORICAL_COMMANDS,
	SGAD_SNAPSHOT_COMMANDS,
	SGAD_SNAPSHOT_POSES,
	SGAD_SNAPSHOT_FINAL_MATCHES,
	SGAD_SNAPSHOT_FINAL_MISMATCHES,
	SGAD_SNAPSHOT_HISTORICAL_PENDING,
	SGAD_SNAPSHOT_REDUCER_ACTIVE,
	SGAD_SNAPSHOT_REDUCER_LINK,
	SGAD_SNAPSHOT_REDUCER_STATUS,
	SGAD_SNAPSHOT_REDUCER_REASON,
	SGAD_SNAPSHOT_REDUCER_ELAPSED,
	SGAD_SNAPSHOT_REDUCER_ARRIVAL,
	SGAD_SNAPSHOT_REDUCER_PENDING,
	SGAD_SNAPSHOT_REDUCER_WALKOFF,
	SGAD_SNAPSHOT_REDUCER_AIRBORNE,
	SGAD_SNAPSHOT_REDUCER_RECOVERY,
	SGAD_SNAPSHOT_OBSERVER_ACTIVE,
	SGAD_SNAPSHOT_OBSERVER_STATUS,
	SGAD_SNAPSHOT_OBSERVER_REASON,
	SGAD_SNAPSHOT_OBSERVER_ELAPSED,
	SGAD_SNAPSHOT_OBSERVER_ARRIVAL,
	SGAD_SNAPSHOT_OBSERVER_PENDING,
	SGAD_SNAPSHOT_OBSERVER_WALKOFF,
	SGAD_SNAPSHOT_OBSERVER_AIRBORNE,
	SGAD_SNAPSHOT_OBSERVER_RECOVERY,
	SGAD_SNAPSHOT_OBSERVER_PRESTEPS,
	SGAD_SNAPSHOT_OBSERVER_POSTSTEPS,
	SGAD_SNAPSHOT_OBSERVER_BOUNDARIES,
	SGAD_SNAPSHOT_OBSERVER_MATCHES,
	SGAD_SNAPSHOT_OBSERVER_MISMATCHES,
	SGAD_SNAPSHOT_ORIGIN,
	SGAD_SNAPSHOT_VELOCITY,
	SGAD_SNAPSHOT_OLD_ORIGIN,
	SGAD_SNAPSHOT_OLDVELOCITY,
	SGAD_SNAPSHOT_PMOVE_ORIGIN,
	SGAD_SNAPSHOT_PMOVE_VELOCITY,
	SGAD_SNAPSHOT_GEOMETRY_QUERY,
	SGAD_SNAPSHOT_TERMINAL_GEOMETRY,
	SGAD_SNAPSHOT_RECOVERY_GEOMETRY,
	SGAD_SNAPSHOT_CAPTURE_READY,
	SGAD_SNAPSHOT_FIXTURE_BOUNDARY,
	SGAD_SNAPSHOT_BIT_COUNT
} sg_accept_drop_snapshot_bit_t;

#define SGAD_SNAPSHOT_MASK(bit) (UINT64_C(1) << (bit))

/* Retired injected cases return to the ordinary navigation cadence, not the
 * proved DROP cadence.  The host-free build authenticates sg_subframes' frozen
 * default and the live Begin/End parameters must report the resulting eight
 * commands over one complete 100 ms outer frame. */
enum
{
	SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS = 8,
	SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC = 100
};

typedef struct sg_accept_drop_selector_s
{
	int source;
	int destination;
	byte action;
	byte provenance;
	byte min_speed;
	byte heading;
	byte heading_slack;
	byte exit_speed;
	short cost_ms;
	uint32_t anchor_bits[3];
	uint32_t source_bits[3];
	uint32_t destination_bits[3];
	short source_area_hint;
	short source_flags;
	short destination_area_hint;
	short destination_flags;
	int expected_link;
	qboolean recovery_required;
	int recovery_start_ms;
	int fixture_seed;
	uint32_t fixture_bits[3];
	short fixture_area_hint;
	short fixture_flags;
	int injection_step;
	qboolean required_airborne;
	sg_accept_drop_fixture_t fixture_kind;
	sg_accept_drop_fixture_boundary_t fixture_boundary;
	qboolean terminal_geometry;
	qboolean recovery_geometry;
	sg_accept_drop_checkpoint_t legacy_checkpoint;
	sg_accept_drop_checkpoint_t rev2_checkpoint;
	const char *name;
} sg_accept_drop_selector_t;

typedef struct sg_accept_drop_post_command_snapshot_s
{
	qboolean captured;
	int frame;
	int step;
	uint32_t origin_bits[3];
	uint32_t velocity_bits[3];
	uint32_t old_origin_bits[3];
	uint32_t oldvelocity_bits[3];
	short pmove_origin[3];
	short pmove_velocity[3];
	int pmove_type;
	int pmove_gravity;
	int pmove_flags;
	int health;
	int deadflag;
	int movetype;
	qboolean grounded;
	const edict_t *groundentity;
	int groundentity_linkcount;
	qboolean support_valid;
	int watertype;
	int waterlevel;
	qboolean terminal_geometry;
	qboolean recovery_geometry;
	unsigned int historical_commands;
	unsigned int commands;
	unsigned int poses;
	unsigned int final_historical_matches;
	unsigned int final_historical_mismatches;
	qboolean historical_pending;
	qboolean reducer_active;
	int reducer_link;
	sg_replay_status_t reducer_status;
	sg_replay_reason_t reducer_reason;
	int reducer_elapsed_ms;
	int reducer_arrival_ms;
	qboolean reducer_step_pending;
	qboolean reducer_walkoff;
	qboolean reducer_airborne;
	qboolean reducer_recovery;
	qboolean observer_active;
	sg_replay_status_t observer_status;
	sg_replay_reason_t observer_reason;
	int observer_elapsed_ms;
	int observer_arrival_ms;
	qboolean observer_step_pending;
	qboolean observer_walkoff;
	qboolean observer_airborne;
	qboolean observer_recovery;
	unsigned int observer_presteps;
	unsigned int observer_poststeps;
	unsigned int observer_boundaries;
	unsigned int observer_command_matches;
	unsigned int observer_command_mismatches;
} sg_accept_drop_post_command_snapshot_t;

typedef struct sg_accept_drop_state_s
{
	int phase;
	int observed_run;
	int requested_case;
	int requested_slot;
	int requested_link;
	struct sg_bot_s *bot;
	int link;
	qboolean armed;
	qboolean started;
	qboolean finished;
	qboolean saw_walkoff;
	qboolean saw_airborne;
	qboolean saw_recovery;
	sg_drop_replay_state_t observer;
	qboolean observer_active;
	qboolean observer_began;
	/* The pure legacy-A observer receives the same post-command event snapshot
	 * that production revision 2 would consume.  Command four remains here
	 * until the next entity/pusher boundary, matching the live adapter law. */
	sg_drop_live_events_t observer_events;
	qboolean observer_events_pending;
	int observer_events_step;
	qboolean boundary_capture_open;
	qboolean boundary_arrival_sampled;
	qboolean boundary_arrival_result;
	qboolean boundary_recovery_sampled;
	qboolean boundary_recovery_result;
	qboolean historical_pending;
	int historical_step;
	usercmd_t historical_command;
	unsigned long sequence;
	unsigned int action_begins;
	unsigned int action_begin_errors;
	unsigned int historical_commands;
	unsigned int commands;
	unsigned int zero_final_commands;
	unsigned int final_historical_matches;
	unsigned int final_historical_mismatches;
	unsigned int arm_poses;
	unsigned int poses;
	unsigned int pusher_begins;
	unsigned int pusher_ends;
	unsigned int pusher_depth;
	unsigned int pusher_order_errors;
	unsigned int observer_presteps;
	unsigned int observer_poststeps;
	unsigned int observer_boundaries;
	unsigned int observer_command_matches;
	unsigned int observer_command_mismatches;
	unsigned int boundary_enters;
	unsigned int boundary_exits;
	unsigned int boundary_results;
	unsigned int result_arrival_samples;
	unsigned int result_arrivals;
	unsigned int result_recovery_samples;
	unsigned int result_recovery_ready;
	unsigned int result_recovery_started;
	unsigned int arrival_callbacks;
	unsigned int recovery_callbacks;
	unsigned int arrival_predicates;
	unsigned int recovery_predicates;
	unsigned int arrival_predicate_results;
	unsigned int recovery_predicate_results;
	unsigned int arrival_predicate_true;
	unsigned int recovery_predicate_true;
	unsigned int arrival_traces;
	unsigned int recovery_traces;
	unsigned int arrival_trace_true;
	unsigned int recovery_trace_true;
	unsigned int observer_arrival_cached;
	unsigned int observer_arrival_inferred;
	unsigned int observer_arrival_cached_true;
	unsigned int observer_recovery_cached;
	unsigned int observer_recovery_inferred;
	unsigned int observer_recovery_cached_true;
	unsigned int shelves;
	unsigned int teaches;
	unsigned int handoffs;
	unsigned int injection_attempts;
	unsigned int injection_applied;
	unsigned int injection_deferrals;
	unsigned int injection_deferral_events;
	unsigned int injection_deferral_order_errors;
	unsigned int injection_deferral_last_ordinal;
	unsigned int injection_zero_ms;
	unsigned int injection_errors;
	unsigned int injection_pmove_traces;
	unsigned int injection_pointcontents;
	unsigned int injection_boundary_checks;
	unsigned int injection_post_command_captures;
	unsigned int injection_post_command_validations;
	unsigned int injection_order_errors;
	uint64_t injection_snapshot_mismatch_mask;
	qboolean injection_pre_contact_captured;
	unsigned int injection_pre_arrival_samples;
	qboolean pre_contact_boundary_open;
	unsigned int pre_contact_boundary_ordinal;
	unsigned int pre_contact_validated;
	unsigned int pre_contact_sampled;
	unsigned int pre_contact_last_sampled;
	unsigned int pre_contact_errors;
	unsigned int pre_contact_arrival_callbacks;
	unsigned int pre_contact_recovery_callbacks;
	unsigned int pre_contact_arrival_predicates;
	unsigned int pre_contact_recovery_predicates;
	unsigned int pre_contact_arrival_predicate_results;
	unsigned int pre_contact_recovery_predicate_results;
	unsigned int pre_contact_arrival_predicate_true;
	unsigned int pre_contact_recovery_predicate_true;
	unsigned int pre_contact_arrival_traces;
	unsigned int pre_contact_recovery_traces;
	unsigned int pre_contact_arrival_trace_true;
	unsigned int pre_contact_recovery_trace_true;
	unsigned int pre_contact_observer_arrival_cached;
	unsigned int pre_contact_observer_arrival_inferred;
	unsigned int pre_contact_observer_arrival_cached_true;
	unsigned int pre_contact_observer_recovery_cached;
	unsigned int pre_contact_observer_recovery_inferred;
	unsigned int pre_contact_observer_recovery_cached_true;
	unsigned int injection_entity_passes;
	unsigned int injection_sg_frames;
	unsigned int private_stops;
	unsigned int generic_handoffs;
	unsigned int generic_handoff_begins;
	unsigned int generic_handoff_ends;
	unsigned int generic_handoff_completed_substeps;
	int injection_step;
	int injection_frame;
	int checkpoint_frame;
	int injection_order_stage;
	int injection_fixture_seed;
	qboolean injection_pre_walkoff;
	qboolean injection_pre_airborne;
	qboolean injection_pre_recovery;
	uint32_t injection_origin_bits[3];
	qboolean injection_grounded;
	qboolean injection_support_valid;
	int injection_watertype;
	int injection_waterlevel;
	int injection_health;
	int injection_deadflag;
	int injection_movetype;
	qboolean injection_oldvelocity_zero;
	qboolean injection_terminal_geometry;
	qboolean injection_recovery_geometry;
	sg_accept_drop_post_command_snapshot_t post_command;
	qboolean stop_before_emit;
	qboolean generic_handoff_pending;
	qboolean generic_handoff_begin_valid;
	int generic_handoff_frame;
	int generic_handoff_bestlink;
	int generic_handoff_substeps;
	int generic_handoff_total_msec;
	usercmd_t generic_handoff_command;
	sg_accept_drop_checkpoint_t checkpoint;
	qboolean last_arrival;
	qboolean last_recovery;
	sg_replay_status_t production_status;
	sg_replay_reason_t production_reason;
	int production_elapsed_ms;
	int production_arrival_ms;
	int production_recovery_start_ms;
	int observer_recovery_start_ms;
	int legacy_recovery_start_ms;
	sg_drop_live_outcome_t final_outcome;
	sg_replay_reason_t final_reason;
	const char *finish_diagnostic;
} sg_accept_drop_state_t;

enum { SGAD_IDLE = 0, SGAD_QUEUED, SGAD_ACTIVE, SGAD_FINISHED, SGAD_FAILED };

static sg_accept_drop_state_t accept_drop;
static int accept_epoch;

void SG_AcceptDropResetLifeActions(struct sg_bot_s *bot);
static void AcceptCompleteInjected(struct sg_bot_s *bot, const char *where);
static void AcceptRejectInjected(struct sg_bot_s *bot, const char *diagnostic,
	const char *where);
static qboolean AcceptLateAirborneSelector(
	const sg_accept_drop_selector_t *selector);

static const sg_accept_drop_selector_t accept_selectors[] = {
	/* case 1: generator-observed direct completion at 500 ms. */
	{ 403, 614, RL_DROP, RL_PROVEN, 0, 109,
	  RUNE_DROP_CONTROL_MARKER, 74, 500,
	  { UINT32_C(0x43821b6c), UINT32_C(0x44990c92), UINT32_C(0x43900400) },
	  { UINT32_C(0x43d38000), UINT32_C(0x448ee000), UINT32_C(0x438c0400) },
	  { UINT32_C(0x43938000), UINT32_C(0x4496e000), UINT32_C(0x43780800) },
	  51, 0, 255, 0, 7666, false, 0,
	  -1, { 0, 0, 0 }, 0, 0, -1, false, SGAD_FIXTURE_NONE,
	  SGAD_FIXTURE_BOUNDARY_NONE, false, false,
	  SGAD_CHECKPOINT_NONE, SGAD_CHECKPOINT_NONE,
	  "direct" },
	/* case 2: generator-observed dry shelf recovery beginning at 1100 ms. */
	{ 1, 129, RL_DROP, RL_PROVEN, 0, 236,
	  RUNE_DROP_CONTROL_MARKER, 74, 1400,
	  { UINT32_C(0x432de441), UINT32_C(0xc29ee80a), UINT32_C(0x43900400) },
	  { UINT32_C(0x43000000), UINT32_C(0xc25c0000), UINT32_C(0x438c0400) },
	  { UINT32_C(0x43848000), UINT32_C(0xc3000000), UINT32_C(0x41c04000) },
	  0, 0, 51, 0, 15, true, 1100,
	  -1, { 0, 0, 0 }, 0, 0, -1, false, SGAD_FIXTURE_NONE,
	  SGAD_FIXTURE_BOUNDARY_NONE, false, false,
	  SGAD_CHECKPOINT_NONE, SGAD_CHECKPOINT_NONE,
	  "recovery" },
	/* case 3: inject dry support before any true airborne pose. */
	{ 445, 40, RL_DROP, RL_PROVEN, 0, 138,
	  RUNE_DROP_CONTROL_MARKER, 74, 1200,
	  { UINT32_C(0x43165a34), UINT32_C(0x44a3abcd), UINT32_C(0x43400800) },
	  { UINT32_C(0x43200000), UINT32_C(0x44a40000), UINT32_C(0x43380800) },
	  { UINT32_C(0xc3400000), UINT32_C(0x44980000), UINT32_C(0xc367f800) },
	  153, 0, 102, 0, 8556, false, 0,
	  136, { UINT32_C(0xc1100000), UINT32_C(0x44500000),
	         UINT32_C(0xc367f800) },
	  127, 0, 2, false, SGAD_FIXTURE_DRY_SUPPORTED,
	  SGAD_FIXTURE_BOUNDARY_POST_COMMAND, false, false,
	  SGAD_CHECKPOINT_LEGACY_SHORT_CONTACT,
	  SGAD_CHECKPOINT_REV2_RUNNING, "supported-preair" },
	/* case 4: a dry recovery shelf for a wet destination. */
	{ 5, 896, RL_DROP, RL_PROVEN, 0, 75,
	  RUNE_DROP_CONTROL_MARKER, 71, 1500,
	  { UINT32_C(0xc21f5f0e), UINT32_C(0x445aead9), UINT32_C(0x43200800) },
	  { UINT32_C(0xc1100000), UINT32_C(0x44400000), UINT32_C(0x43180800) },
	  { UINT32_C(0xc2ee0000), UINT32_C(0x44900000), UINT32_C(0xc387fc00) },
	  31, 0, 72, 1, 85, false, 0,
	  144, { UINT32_C(0xc3370000), UINT32_C(0x44900000),
	         UINT32_C(0xc367f800) },
	  42, 0, 3, true, SGAD_FIXTURE_DRY_SUPPORTED,
	  SGAD_FIXTURE_BOUNDARY_EXACT_SEED, false, true,
	  SGAD_CHECKPOINT_LEGACY_WET_RECOVERY,
	  SGAD_CHECKPOINT_REV2_SHORT_LANDING, "wet-target-dry-shelf" },
	/* case 5: depth-two water inside dry recovery geometry. */
	{ 5, 144, RL_DROP, RL_PROVEN, 0, 81,
	  RUNE_DROP_CONTROL_MARKER, 74, 1500,
	  { UINT32_C(0xc27a9eab), UINT32_C(0x445d9a4d), UINT32_C(0x43200800) },
	  { UINT32_C(0xc1100000), UINT32_C(0x44400000), UINT32_C(0x43180800) },
	  { UINT32_C(0xc3370000), UINT32_C(0x44900000), UINT32_C(0xc367f800) },
	  31, 0, 42, 0, 66, false, 0,
	  896, { UINT32_C(0xc2ee0000), UINT32_C(0x44900000),
	         UINT32_C(0xc387fc00) },
	  72, 1, 3, true, SGAD_FIXTURE_WATER_DEPTH2,
	  SGAD_FIXTURE_BOUNDARY_EXACT_SEED, false, true,
	  SGAD_CHECKPOINT_LEGACY_SHORT_CONTACT,
	  SGAD_CHECKPOINT_REV2_SHORT_LANDING, "dry-target-depth2" }
};

#define SG_ACCEPT_DROP_CASE_COUNT \
	((int)(sizeof(accept_selectors) / sizeof(accept_selectors[0])))
#define SG_ACCEPT_DROP_NATURAL_CASE_COUNT 2
#define SG_ACCEPT_DROP_SUMMARY_PARTS 9
#define SG_ACCEPT_DROP_SUMMARY_LINE_MAX 700
#define SG_ACCEPT_DROP_SUMMARY_LINE_CAP 768

static uint32_t AcceptFloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static uint32_t AcceptFixturePmoveBits(uint32_t rune_bits)
{
	float rune_value;
	float fixed_value;
	short fixed;

	memcpy(&rune_value, &rune_bits, sizeof(rune_value));
	fixed = (short)(rune_value * 8.0f);
	fixed_value = fixed * 0.125f;
	return AcceptFloatBits(fixed_value);
}

static qboolean AcceptHeaderMatches(const rune_t *rune)
{
	static const char map_name[SG_RUNE_V3_MAP_NAME_BYTES] = "lmctf14";
	const sg_rune_v3_header_t *header;

	if (!rune || !rune->seeds || !rune->links)
		return false;
	header = &rune->v3_header;
	if (header->magic != SG_RUNE_V3_MAGIC ||
	    header->version != SG_RUNE_V3_VERSION ||
	    header->header_bytes != SG_RUNE_V3_HEADER_BYTES ||
	    header->seed_bytes != SG_RUNE_V3_SEED_BYTES ||
	    header->link_bytes != SG_RUNE_V3_LINK_BYTES ||
	    header->num_seeds != UINT32_C(960) ||
	    header->num_links != UINT32_C(24451) ||
	    header->payload_crc32 != UINT32_C(0xd3d0ca2f) ||
	    header->bsp_checksum != UINT32_C(0x0e7c5adf) ||
	    header->entity_crc32 != UINT32_C(0xbdb3e621) ||
	    header->action_contract_crc32 != UINT32_C(0x5c64bc3b) ||
	    header->physics_flags != UINT32_C(0) ||
	    AcceptFloatBits(header->gravity) != UINT32_C(0x44480000) ||
	    AcceptFloatBits(header->airaccelerate) != UINT32_C(0) ||
	    AcceptFloatBits(header->maxvelocity) != UINT32_C(0x44fa0000) ||
	    header->pmove_substep_ms != UINT16_C(25) ||
	    header->server_frame_ms != UINT16_C(100) ||
	    header->host_physics_id != UINT32_C(1) ||
	    header->header_crc32 != UINT32_C(0x1d5e73c6) ||
	    memcmp(header->map_name, map_name, sizeof(map_name)) != 0)
		return false;
	return rune->hdr.magic == (int)SG_RUNE_V3_MAGIC &&
	       rune->hdr.version == SG_RUNE_V3_VERSION &&
	       rune->hdr.num_seeds == 960 && rune->hdr.num_links == 24451 &&
	       memcmp(rune->hdr.mapname, map_name, sizeof(map_name)) == 0;
}

static const char *AcceptVariant(void)
{
	return SG_ACCEPT_DROP_LEGACY_A ? "A-legacy" : "B-rev2";
}

static const char *AcceptCheckpointToken(sg_accept_drop_checkpoint_t checkpoint)
{
	switch (checkpoint)
	{
	case SGAD_CHECKPOINT_NONE: return "none";
	case SGAD_CHECKPOINT_LEGACY_SHORT_CONTACT:
		return "legacy-short-contact";
	case SGAD_CHECKPOINT_LEGACY_WET_RECOVERY:
		return "legacy-wet-recovery";
	case SGAD_CHECKPOINT_REV2_RUNNING: return "rev2-running";
	case SGAD_CHECKPOINT_REV2_SHORT_LANDING:
		return "rev2-short-landing";
	default: return "unknown";
	}
}

static const char *AcceptFixtureBoundaryToken(
	sg_accept_drop_fixture_boundary_t boundary)
{
	switch (boundary)
	{
	case SGAD_FIXTURE_BOUNDARY_NONE: return "none";
	case SGAD_FIXTURE_BOUNDARY_EXACT_SEED: return "exact-seed";
	case SGAD_FIXTURE_BOUNDARY_POST_COMMAND: return "post-command";
	default: return "unknown";
	}
}

static qboolean AcceptEnabled(void)
{
	return (accept_drop.phase == SGAD_QUEUED ||
	        accept_drop.phase == SGAD_ACTIVE) &&
	       accept_drop.requested_case >= 1 &&
	       accept_drop.requested_case <= SG_ACCEPT_DROP_CASE_COUNT;
}

static void AcceptLogPrefix(const char *event)
{
	accept_drop.sequence++;
	sg_host.dprint("SG_ACCEPT_DROP seq=%lu variant=%s run=%d case=%d "
	               "frame=%d event=%s", accept_drop.sequence, AcceptVariant(),
	               accept_drop.observed_run, accept_drop.requested_case,
	               level.framenum, event ? event : "?");
}

static qboolean AcceptLinkMatches(const rune_t *rune, int index,
	const sg_accept_drop_selector_t *selector)
{
	const rune_link_t *link;
	int axis;

	if (!rune || !selector || index < 0 || index >= rune->hdr.num_links)
		return false;
	link = &rune->links[index];
	if (link->from != selector->source || link->to != selector->destination ||
	    link->action != selector->action ||
	    link->provenance != selector->provenance ||
	    link->min_speed != selector->min_speed ||
	    link->heading != selector->heading ||
	    link->heading_slack != selector->heading_slack ||
	    link->exit_speed != selector->exit_speed ||
	    link->cost_ms != selector->cost_ms ||
	    rune->seeds[link->from].area_hint != selector->source_area_hint ||
	    rune->seeds[link->from].flags != selector->source_flags ||
	    rune->seeds[link->to].area_hint != selector->destination_area_hint ||
	    rune->seeds[link->to].flags != selector->destination_flags)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (AcceptFloatBits(link->anchor[axis]) != selector->anchor_bits[axis] ||
		    AcceptFloatBits(rune->seeds[link->from].origin[axis]) !=
		        selector->source_bits[axis] ||
		    AcceptFloatBits(rune->seeds[link->to].origin[axis]) !=
		        selector->destination_bits[axis])
			return false;
	return true;
}

static int AcceptFindLink(const rune_t *rune,
	const sg_accept_drop_selector_t *selector)
{
	int found = -1;
	int count = 0;
	int index;

	if (!selector || !AcceptHeaderMatches(rune))
		return -1;
	/* The strict v3 loader has already rejected nonzero mechanism anchors,
	 * sweep_clear_ms, mode and reserved for ordinary noncompound RL_DROP.
	 * Those wire-only fields are deliberately discarded by rune_link_t; the
	 * exact authenticated payload CRC above binds their zero bytes at arm time. */
	for (index = 0; index < rune->hdr.num_links; index++)
		if (AcceptLinkMatches(rune, index, selector))
		{
			found = index;
			count++;
		}
	return count == 1 ? found : -1;
}

static qboolean AcceptWithinGeometry(const vec3_t origin,
	const vec3_t destination, float radius, float z_limit)
{
	float dx, dy, dz;

	dx = destination[0] - origin[0];
	dy = destination[1] - origin[1];
	dz = destination[2] - origin[2];
	return dx * dx + dy * dy < radius * radius &&
	       dz > -z_limit && dz < z_limit;
}

static qboolean AcceptFixtureMatches(const rune_t *rune,
	const sg_accept_drop_selector_t *selector)
{
	const rune_seed_t *fixture;
	const rune_seed_t *destination;
	int axis;

	if (!rune || !selector)
		return false;
	if (selector->fixture_kind == SGAD_FIXTURE_NONE)
		return selector->fixture_seed == -1 && selector->injection_step == -1 &&
		       selector->fixture_boundary == SGAD_FIXTURE_BOUNDARY_NONE &&
		       selector->legacy_checkpoint == SGAD_CHECKPOINT_NONE &&
		       selector->rev2_checkpoint == SGAD_CHECKPOINT_NONE;
	if (selector->fixture_seed < 0 ||
	    selector->fixture_seed >= rune->hdr.num_seeds ||
	    selector->destination < 0 ||
	    selector->destination >= rune->hdr.num_seeds ||
	    selector->injection_step !=
	        (selector == &accept_selectors[2] ? 2 : 3) ||
	    selector->fixture_boundary !=
	        (selector == &accept_selectors[2] ?
	            SGAD_FIXTURE_BOUNDARY_POST_COMMAND :
	            SGAD_FIXTURE_BOUNDARY_EXACT_SEED))
		return false;
	fixture = &rune->seeds[selector->fixture_seed];
	destination = &rune->seeds[selector->destination];
	if (fixture->area_hint != selector->fixture_area_hint ||
	    fixture->flags != selector->fixture_flags)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (AcceptFloatBits(fixture->origin[axis]) !=
		    selector->fixture_bits[axis])
			return false;
	if (AcceptWithinGeometry(fixture->origin, destination->origin,
	        SG_REPLAY_ARRIVE_RADIUS, SG_REPLAY_ARRIVE_Z) !=
	    selector->terminal_geometry)
		return false;
	if (AcceptWithinGeometry(fixture->origin, destination->origin,
	        SG_RUNE_PROOF_DROP_RECOVERY_RADIUS,
	        SG_RUNE_PROOF_DROP_RECOVERY_Z) != selector->recovery_geometry)
		return false;
	return true;
}

static qboolean AcceptOwns(const struct sg_bot_s *bot, int link_index)
{
	return AcceptEnabled() && accept_drop.armed && bot == accept_drop.bot &&
	       link_index == accept_drop.link;
}

static const char *AcceptReplayReasonToken(sg_replay_reason_t reason)
{
	static const char *const tokens[] = {
		"none",
		"invalid-argument",
		"invalid-state",
		"invalid-control",
		"non-finite-pose",
		"contaminated",
		"door-passed",
		"hazardous-liquid",
		"damaging-fall",
		"drop-approach-timeout",
		"drop-travel-timeout",
		"action-timeout",
		"below-destination",
		"shallow-water-contact",
		"short-landing",
		"recovery-support-lost",
		"zero-time-arrival",
		"timing-mismatch",
		"hook-attach-timing",
		"hook-event-order",
		"hook-release-before-pull",
		"hook-release-missed",
		"hook-pull-timeout",
		"hook-settle-timeout",
		"hook-terminal-lost"
	};
	int index = (int)reason;
	_Static_assert(sizeof(tokens) / sizeof(tokens[0]) ==
	    SG_REPLAY_REASON_HOOK_TERMINAL_LOST + 1,
	    "replay reason token table must cover every enum value");

	if (index < 0 || index >= (int)(sizeof(tokens) / sizeof(tokens[0])))
		return "unknown";
	return tokens[index];
}

/* SG_ReplayReasonName is display text and some names contain whitespace.
 * Event values are a token protocol, including the Shelf/Teach adapter whose
 * callers historically supplied either display text or a fixed reason. */
static const char *AcceptEventReasonToken(const char *reason)
{
	int index;

	if (!reason)
		return "unknown";
	for (index = 0; index <= SG_REPLAY_REASON_HOOK_TERMINAL_LOST; index++)
	{
		const char *token = AcceptReplayReasonToken((sg_replay_reason_t)index);

		if (strcmp(reason, token) == 0 ||
		    strcmp(reason, SG_ReplayReasonName((sg_replay_reason_t)index)) == 0)
			return token;
	}
	if (strcmp(reason, "contact-short") == 0 ||
	    strcmp(reason, "recovery-lost") == 0 ||
	    strcmp(reason, "command-recovery-lost") == 0)
		return reason;
	return "unknown";
}

static const char *AcceptDiagnosticToken(const char *diagnostic)
{
	static const char *const tokens[] = {
		"none",
		"finish-invalid-expectation",
		"finish-action-count",
		"finish-command-count",
		"finish-final-command-zeroed",
		"finish-pose-count",
		"finish-pusher-order",
		"finish-shelf-teach",
		"finish-handoff-evidence",
		"finish-recovery-evidence",
		"finish-unexpected-recovery",
		"finish-arrival-evidence",
		"finish-contact-history",
		"finish-boundary-count",
		"finish-observer-terminal",
		"finish-observer-command",
		"finish-observer-boundary",
		"finish-observer-host-call",
		"finish-observer-contact-cache",
		"finish-recovery-timing",
		"finish-production-terminal",
		"finish-production-boundary",
		"finish-production-contact",
		"finish-production-result",
		"finish-unexpected-observer",
		"finish-injection-count",
		"finish-injection-precondition",
		"finish-injection-fixture",
		"finish-injection-order",
		"finish-injection-checkpoint",
		"finish-private-stop"
	};
	size_t index;

	if (!diagnostic)
		return "none";
	for (index = 0; index < sizeof(tokens) / sizeof(tokens[0]); index++)
		if (strcmp(diagnostic, tokens[index]) == 0)
			return tokens[index];
	return "unknown";
}

static int AcceptSummaryFormatPart(char *record, size_t capacity,
	const sg_accept_drop_state_t *state,
	const sg_accept_drop_selector_t *selector, const char *terminal,
	int part, unsigned long sequence, int frame, const char *variant)
{
	const char *diagnostic;
	const char *final_reason;
	const char *observer_reason;
	const char *production_reason;

	if (!record || capacity == 0 || !state || !selector || !terminal ||
	    !variant || part < 0 || part >= SG_ACCEPT_DROP_SUMMARY_PARTS)
		return -1;
	diagnostic = AcceptDiagnosticToken(state->finish_diagnostic);
	final_reason = AcceptReplayReasonToken(state->final_reason);
	observer_reason = AcceptReplayReasonToken(state->observer.progress.reason);
	production_reason = AcceptReplayReasonToken(state->production_reason);

	switch (part)
	{
	case 0:
		return snprintf(record, capacity,
		    "SG_ACCEPT_DROP seq=%lu variant=%s run=%d case=%d frame=%d "
		    "event=summary-begin summary_set=%d part=1/9 selector=%s "
		    "link=%d started=%d fixture_seed=%d injection_attempts=%u "
		    "injection_applied=%u injection_zero_ms=%u injection_errors=%u "
		    "injection_step=%d expected_checkpoint=%s checkpoint=%s "
		    "generic_handoff_begins=%u generic_handoff_ends=%u "
		    "generic_completed_substeps=%u generic_handoff_pending=%d "
		    "generic_begin_valid=%d generic_begin_substeps=%d "
		    "generic_total_msec=%d\n",
		    sequence, variant, state->observed_run, state->requested_case,
		    frame, state->observed_run, selector->name, state->link,
		    state->started, state->injection_fixture_seed,
		    state->injection_attempts, state->injection_applied,
		    state->injection_zero_ms, state->injection_errors,
		    state->injection_step,
		    AcceptCheckpointToken(SG_ACCEPT_DROP_LEGACY_A ?
		        selector->legacy_checkpoint : selector->rev2_checkpoint),
		    AcceptCheckpointToken(state->checkpoint),
		    state->generic_handoff_begins, state->generic_handoff_ends,
		    state->generic_handoff_completed_substeps,
		    state->generic_handoff_pending,
		    state->generic_handoff_begin_valid,
		    state->generic_handoff_substeps,
		    state->generic_handoff_total_msec);
	case 1:
		return snprintf(record, capacity,
		    "SG_ACCEPT_DROP seq=%lu variant=%s run=%d case=%d frame=%d "
		    "event=summary-command summary_set=%d part=2/9 "
		    "action_begins=%u action_begin_errors=%u historical_commands=%u "
		    "final_commands=%u zero_final_commands=%u "
		    "final_historical_matches=%u final_historical_mismatches=%u "
		    "historical_pending=%d arm_poses=%u poses_25ms=%u "
		    "pusher_begins=%u pusher_ends=%u pusher_depth=%u "
		    "pusher_order_errors=%u\n",
		    sequence, variant, state->observed_run, state->requested_case,
		    frame, state->observed_run, state->action_begins,
		    state->action_begin_errors, state->historical_commands,
		    state->commands, state->zero_final_commands,
		    state->final_historical_matches,
		    state->final_historical_mismatches, state->historical_pending,
		    state->arm_poses, state->poses, state->pusher_begins,
		    state->pusher_ends, state->pusher_depth,
		    state->pusher_order_errors);
	case 2:
		return snprintf(record, capacity,
		    "SG_ACCEPT_DROP seq=%lu variant=%s run=%d case=%d frame=%d "
		    "event=summary-contact summary_set=%d part=3/9 "
		    "arrival_callback=%u recovery_callback=%u "
		    "arrival_predicate=%u recovery_predicate=%u "
		    "arrival_predicate_results=%u recovery_predicate_results=%u "
		    "arrival_predicate_true=%u recovery_predicate_true=%u "
		    "arrival_trace=%u recovery_trace=%u arrival_trace_true=%u "
		    "recovery_trace_true=%u shelves=%u teaches=%u handoffs=%u "
		    "walkoff=%d airborne=%d recovery=%d\n",
		    sequence, variant, state->observed_run, state->requested_case,
		    frame, state->observed_run, state->arrival_callbacks,
		    state->recovery_callbacks, state->arrival_predicates,
		    state->recovery_predicates, state->arrival_predicate_results,
		    state->recovery_predicate_results, state->arrival_predicate_true,
		    state->recovery_predicate_true, state->arrival_traces,
		    state->recovery_traces, state->arrival_trace_true,
		    state->recovery_trace_true, state->shelves, state->teaches,
		    state->handoffs, state->saw_walkoff, state->saw_airborne,
		    state->saw_recovery);
	case 3:
		return snprintf(record, capacity,
		    "SG_ACCEPT_DROP seq=%lu variant=%s run=%d case=%d frame=%d "
		    "event=summary-boundary summary_set=%d part=4/9 "
		    "boundary_enters=%u boundary_exits=%u boundary_results=%u "
		    "result_arrival_samples=%u result_arrivals=%u "
		    "result_recovery_samples=%u result_recovery_ready=%u "
		    "result_recovery_started=%u "
		    "pre_contact_validated=%u pre_contact_sampled=%u "
		    "pre_contact_last_sampled=%u pre_contact_errors=%u "
		    "pre_contact_captured=%d pre_arrival_samples=%u\n",
		    sequence, variant, state->observed_run, state->requested_case,
		    frame, state->observed_run, state->boundary_enters,
		    state->boundary_exits, state->boundary_results,
		    state->result_arrival_samples, state->result_arrivals,
		    state->result_recovery_samples, state->result_recovery_ready,
		    state->result_recovery_started,
		    state->pre_contact_validated, state->pre_contact_sampled,
		    state->pre_contact_last_sampled, state->pre_contact_errors,
		    state->injection_pre_contact_captured,
		    state->injection_pre_arrival_samples);
	case 4:
		return snprintf(record, capacity,
		    "SG_ACCEPT_DROP seq=%lu variant=%s run=%d case=%d frame=%d "
		    "event=summary-injection-order summary_set=%d part=5/9 "
		    "injection_frame=%d checkpoint_frame=%d order_stage=%d "
		    "order_errors=%u entity_passes=%u sg_frames=%u "
		    "fixture_boundary_checks=%u fixture_boundary_mode=%s "
		    "post_command_captures=%u post_command_validations=%u\n",
		    sequence, variant, state->observed_run, state->requested_case,
		    frame, state->observed_run, state->injection_frame,
		    state->checkpoint_frame, state->injection_order_stage,
		    state->injection_order_errors, state->injection_entity_passes,
		    state->injection_sg_frames, state->injection_boundary_checks,
		    AcceptFixtureBoundaryToken(selector->fixture_boundary),
		    state->injection_post_command_captures,
		    state->injection_post_command_validations);
	case 5:
		return snprintf(record, capacity,
		    "SG_ACCEPT_DROP seq=%lu variant=%s run=%d case=%d frame=%d "
		    "event=summary-observer summary_set=%d part=6/9 "
		    "observer_began=%d observer_active=%d observer_status=%d "
		    "observer_reason=%s observer_elapsed_ms=%d observer_arrival_ms=%d "
		    "observer_presteps=%u observer_poststeps=%u "
		    "observer_boundaries=%u observer_command_matches=%u "
		    "observer_command_mismatches=%u observer_recovery_start_ms=%d "
		    "legacy_recovery_start_ms=%d\n",
		    sequence, variant, state->observed_run, state->requested_case,
		    frame, state->observed_run, state->observer_began,
		    state->observer_active, (int)state->observer.progress.status,
		    observer_reason, state->observer.progress.elapsed_ms,
		    state->observer.progress.arrival_ms, state->observer_presteps,
		    state->observer_poststeps, state->observer_boundaries,
		    state->observer_command_matches,
		    state->observer_command_mismatches,
		    state->observer_recovery_start_ms,
		    state->legacy_recovery_start_ms);
	case 6:
		return snprintf(record, capacity,
		    "SG_ACCEPT_DROP seq=%lu variant=%s run=%d case=%d frame=%d "
		    "event=summary-observer-contact summary_set=%d part=7/9 "
		    "observer_arrival_cached=%u observer_arrival_inferred=%u "
		    "observer_arrival_cached_true=%u observer_recovery_cached=%u "
		    "observer_recovery_inferred=%u observer_recovery_cached_true=%u "
		    "injection_deferrals=%u deferral_events=%u "
		    "deferral_last_ordinal=%u deferral_order_errors=%u\n",
		    sequence, variant, state->observed_run, state->requested_case,
		    frame, state->observed_run, state->observer_arrival_cached,
		    state->observer_arrival_inferred,
		    state->observer_arrival_cached_true,
		    state->observer_recovery_cached,
		    state->observer_recovery_inferred,
		    state->observer_recovery_cached_true,
		    state->injection_deferrals, state->injection_deferral_events,
		    state->injection_deferral_last_ordinal,
		    state->injection_deferral_order_errors);
	case 7:
		return snprintf(record, capacity,
		    "SG_ACCEPT_DROP seq=%lu variant=%s run=%d case=%d frame=%d "
		    "event=summary-production summary_set=%d part=8/9 "
		    "production_status=%d production_reason=%s "
		    "production_elapsed_ms=%d production_arrival_ms=%d "
		    "production_recovery_start_ms=%d fixture_pmove_traces=%u "
		    "fixture_pointcontents=%u fixture_grounded=%d support_valid=%d "
		    "watertype=%d waterlevel=%d terminal_geometry=%d "
		    "recovery_geometry=%d fixture_health=%d fixture_deadflag=%d "
		    "fixture_movetype=%d oldvelocity_zero=%d private_stops=%u "
		    "generic_handoffs=%u\n",
		    sequence, variant, state->observed_run, state->requested_case,
		    frame, state->observed_run, (int)state->production_status,
		    production_reason, state->production_elapsed_ms,
		    state->production_arrival_ms,
		    state->production_recovery_start_ms,
		    state->injection_pmove_traces, state->injection_pointcontents,
		    state->injection_grounded, state->injection_support_valid,
		    state->injection_watertype, state->injection_waterlevel,
		    state->injection_terminal_geometry,
		    state->injection_recovery_geometry, state->injection_health,
		    state->injection_deadflag, state->injection_movetype,
		    state->injection_oldvelocity_zero, state->private_stops,
		    state->generic_handoffs);
	case 8:
		return snprintf(record, capacity,
		    "SG_ACCEPT_DROP seq=%lu variant=%s run=%d case=%d frame=%d "
		    "event=summary-end summary_set=%d part=9/9 terminal=%s outcome=%d "
		    "reason=%s diagnostic=%s complete=1\n",
		    sequence, variant, state->observed_run, state->requested_case,
		    frame, state->observed_run, terminal, (int)state->final_outcome,
		    final_reason, diagnostic);
	default:
		return -1;
	}
}

static void AcceptLogSummaryFormatFailed(int part)
{
	accept_drop.sequence++;
	sg_host.dprint("SG_ACCEPT_DROP seq=%lu variant=%s run=%d case=%d "
	               "frame=%d event=summary-format-failed summary_set=%d "
	               "part=%d/9 complete=0\n",
	    accept_drop.sequence, AcceptVariant(), accept_drop.observed_run,
	    accept_drop.requested_case, level.framenum, accept_drop.observed_run,
	    part + 1);
}

static void AcceptSummary(const char *terminal)
{
	char records[SG_ACCEPT_DROP_SUMMARY_PARTS]
	    [SG_ACCEPT_DROP_SUMMARY_LINE_CAP];
	const sg_accept_drop_selector_t *selector;
	unsigned long first_sequence;
	int lengths[SG_ACCEPT_DROP_SUMMARY_PARTS];
	int part;

	if (accept_drop.requested_case < 1 ||
	    accept_drop.requested_case > SG_ACCEPT_DROP_CASE_COUNT)
		return;
	selector = &accept_selectors[accept_drop.requested_case - 1];
	first_sequence = accept_drop.sequence + 1;
	for (part = 0; part < SG_ACCEPT_DROP_SUMMARY_PARTS; part++)
	{
		lengths[part] = AcceptSummaryFormatPart(records[part],
		    sizeof(records[part]), &accept_drop, selector,
		    terminal ? terminal : "unknown", part,
		    first_sequence + (unsigned long)part, level.framenum,
		    AcceptVariant());
		if (lengths[part] <= 0 ||
		    lengths[part] >= SG_ACCEPT_DROP_SUMMARY_LINE_MAX ||
		    records[part][lengths[part] - 1] != '\n')
			goto format_failed;
	}
	accept_drop.sequence += SG_ACCEPT_DROP_SUMMARY_PARTS;
	for (part = 0; part < SG_ACCEPT_DROP_SUMMARY_PARTS; part++)
		sg_host.dprint("%s", records[part]);
	return;

format_failed:
	AcceptLogSummaryFormatFailed(part);
}

static qboolean AcceptInt(const char *text, int *value)
{
	char *end = NULL;
	long parsed;

	if (!text || !*text || !value)
		return false;
	errno = 0;
	parsed = strtol(text, &end, 10);
	if (errno || !end || *end || parsed < 0 || parsed > 0x7fffffffL)
		return false;
	*value = (int)parsed;
	return true;
}

qboolean SG_AcceptDropQueue(const char *case_name, const char *slot_text,
	const char *link_text)
{
	int requested_case = 0;
	int slot;
	int link;
	int index;

	for (index = 0; case_name && index < SG_ACCEPT_DROP_CASE_COUNT; index++)
		if (strcmp(case_name, accept_selectors[index].name) == 0)
		{
			requested_case = index + 1;
			break;
		}
	if (!requested_case || !AcceptInt(slot_text, &slot) ||
	    !AcceptInt(link_text, &link) || slot >= SG_MAXBOTS)
		return false;
	if (link != accept_selectors[requested_case - 1].expected_link)
		return false;
	memset(&accept_drop, 0, sizeof(accept_drop));
	accept_drop.phase = SGAD_QUEUED;
	accept_drop.observed_run = ++accept_epoch;
	accept_drop.requested_case = requested_case;
	accept_drop.requested_slot = slot;
	accept_drop.requested_link = link;
	accept_drop.link = -1;
	accept_drop.final_outcome = SG_DROP_LIVE_RUNNING;
	accept_drop.final_reason = SG_REPLAY_REASON_NONE;
	AcceptLogPrefix("queued");
	sg_host.dprint(" selector=%s slot=%d requested_link=%d\n",
	    accept_selectors[requested_case - 1].name, slot, link);
	return true;
}

void SG_AcceptDropLevelReset(void)
{
	memset(&accept_drop, 0, sizeof(accept_drop));
}

void SG_AcceptDropFrameEvent(const char *event)
{
	if (!AcceptEnabled())
		return;
	AcceptLogPrefix(event);
	sg_host.dprint(" link=%d\n", accept_drop.link);
}

void SG_AcceptDropFrameBegin(void)
{
	if (AcceptEnabled() && accept_drop.injection_applied)
	{
		accept_drop.injection_sg_frames++;
		if (accept_drop.injection_order_stage != SGAD_ORDER_ENTITY_PASS ||
		    level.framenum != accept_drop.injection_frame + 1 ||
		    accept_drop.injection_entity_passes != 1 ||
		    accept_drop.pusher_depth != 0)
			accept_drop.injection_order_errors++;
		else
			accept_drop.injection_order_stage = SGAD_ORDER_SG_FRAME;
	}
	SG_AcceptDropFrameEvent("sg-runframe-begin");
}

void SG_AcceptDropEntityPass(void)
{
	if (AcceptEnabled() && accept_drop.injection_applied)
	{
		accept_drop.injection_entity_passes++;
		if (accept_drop.injection_order_stage != SGAD_ORDER_INJECTED ||
		    level.framenum != accept_drop.injection_frame + 1 ||
		    accept_drop.pusher_depth != 0)
			accept_drop.injection_order_errors++;
		else
			accept_drop.injection_order_stage = SGAD_ORDER_ENTITY_PASS;
	}
	SG_AcceptDropFrameEvent("entity-pass-complete");
}

void SG_AcceptDropPusher(const edict_t *ent, const char *phase)
{
	qboolean ending;

	if (!AcceptEnabled())
		return;
	ending = phase && strcmp(phase, "end") == 0;
	if (ending)
	{
		accept_drop.pusher_ends++;
		if (accept_drop.pusher_depth == 0)
			accept_drop.pusher_order_errors++;
		else
			accept_drop.pusher_depth--;
	}
	else
	{
		accept_drop.pusher_begins++;
		accept_drop.pusher_depth++;
	}
	AcceptLogPrefix(ending ? "pusher-end" : "pusher-begin");
	sg_host.dprint(" ent=%d classname=%s pusher_begins=%u pusher_ends=%u "
	               "pusher_depth=%u pusher_order_errors=%u\n",
	    ent ? (int)(ent - g_edicts) : -1,
	    ent && ent->classname ? ent->classname : "?",
	    accept_drop.pusher_begins, accept_drop.pusher_ends,
	    accept_drop.pusher_depth, accept_drop.pusher_order_errors);
}

static void AcceptLogArmed(const sg_accept_drop_selector_t *selector,
	const struct sg_bot_s *bot, int link)
{
	const rune_t *rune = SG_Rune();

	AcceptLogPrefix("armed");
	sg_host.dprint(" selector=%s bot=%d client=%d link=%d from=%d to=%d "
	               "action=%u provenance=%u min_speed=%u heading=%u "
	               "heading_slack=%u exit_speed=%u cost_ms=%d "
	               "anchor_bits=%08x/%08x/%08x "
	               "source_area_hint=%d source_flags=%d "
	               "destination_area_hint=%d destination_flags=%d "
	               "payload_crc32=%08x header_crc32=%08x "
	               "gravity_bits=%08x airaccelerate_bits=%08x "
	               "maxvelocity_bits=%08x fixture_seed=%d "
	               "fixture_bits=%08x/%08x/%08x fixture_area_hint=%d "
	               "fixture_flags=%d injection_step=%d required_airborne=%d "
	               "fixture_boundary_mode=%s terminal_geometry=%d "
	               "recovery_geometry=%d\n",
	    selector->name, accept_drop.requested_slot,
	    (int)(bot->ent - g_edicts), link,
	    selector->source, selector->destination, selector->action,
	    selector->provenance, selector->min_speed, selector->heading,
	    selector->heading_slack, selector->exit_speed, selector->cost_ms,
	    selector->anchor_bits[0], selector->anchor_bits[1],
	    selector->anchor_bits[2], selector->source_area_hint,
	    selector->source_flags, selector->destination_area_hint,
	    selector->destination_flags, rune->v3_header.payload_crc32,
	    rune->v3_header.header_crc32,
	    AcceptFloatBits(rune->v3_header.gravity),
	    AcceptFloatBits(rune->v3_header.airaccelerate),
	    AcceptFloatBits(rune->v3_header.maxvelocity),
	    selector->fixture_seed, selector->fixture_bits[0],
	    selector->fixture_bits[1], selector->fixture_bits[2],
	    selector->fixture_area_hint, selector->fixture_flags,
	    selector->injection_step, selector->required_airborne,
	    AcceptFixtureBoundaryToken(selector->fixture_boundary),
	    selector->terminal_geometry, selector->recovery_geometry);
}

static void AcceptPlaceAtSource(struct sg_bot_s *bot, int link_index)
{
	rune_t *rune = SG_Rune();
	edict_t *ent = bot->ent;
	const rune_link_t *link = &rune->links[link_index];
	vec3_t source;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		short fixed = (short)(rune->seeds[link->from].origin[axis] * 8.0f);
		source[axis] = fixed * 0.125f;
		ent->client->ps.pmove.origin[axis] = fixed;
		ent->client->ps.pmove.velocity[axis] = 0;
	}
	VectorCopy(source, ent->s.origin);
	VectorCopy(source, ent->s.old_origin);
	VectorClear(ent->velocity);
	VectorClear(ent->client->oldvelocity);
	ent->client->ps.pmove.pm_type = PM_NORMAL;
	ent->client->ps.pmove.pm_flags = 0;
	ent->client->ps.pmove.pm_time = 0;
	ent->client->ps.pmove.gravity = (short)sv_gravity->value;
	ent->client->old_pmove = ent->client->ps.pmove;
	ent->waterlevel = 0;
	ent->watertype = 0;
	ent->groundentity = NULL;
	ent->s.event = 0;
	sg_host.linkentity(ent);
}

void SG_AcceptDropArm(void)
{
	const sg_accept_drop_selector_t *selector;
	struct sg_bot_s *bot;
	edict_t *ent;
	usercmd_t zero;
	short fixed[3];
	int health_before;
	int axis;
	int link;

	if (accept_drop.phase != SGAD_QUEUED || !SG_Rune())
		return;
	bot = &sg_bots[accept_drop.requested_slot];
	ent = bot->ent;
	selector = &accept_selectors[accept_drop.requested_case - 1];
	link = AcceptFindLink(SG_Rune(), selector);
	if (link < 0 || link != accept_drop.requested_link ||
	    !AcceptFixtureMatches(SG_Rune(), selector))
	{
		accept_drop.phase = SGAD_FAILED;
		accept_drop.finished = true;
		accept_drop.final_reason = SG_REPLAY_REASON_INVALID_CONTROL;
		AcceptSummary("selector-rejected");
		return;
	}
	if (!bot->active || !ent || !ent->inuse || !ent->client ||
	    !(ent->flags & FL_BOT) || ent->health <= 0 || ent->deadflag ||
	    ent->movetype != MOVETYPE_WALK ||
	    ent->client->ps.pmove.pm_type != PM_NORMAL)
	{
		accept_drop.phase = SGAD_FAILED;
		accept_drop.finished = true;
		AcceptSummary("bot-not-live");
		return;
	}
	if (ent->client->hookstate != 0 || ent->client->hook)
		ctf_hook_abort(ent);
	SG_AcceptDropResetLifeActions(bot);
	accept_drop.bot = bot;
	accept_drop.link = link;
	AcceptPlaceAtSource(bot, link);
	for (axis = 0; axis < 3; axis++)
		fixed[axis] = ent->client->ps.pmove.origin[axis];
	memset(&zero, 0, sizeof(zero));
	health_before = ent->health;
	ClientThink(ent, &zero);
	for (axis = 0; axis < 3; axis++)
		if (ent->client->ps.pmove.origin[axis] != fixed[axis] ||
		    ent->s.origin[axis] != fixed[axis] * 0.125f)
			goto reject_pose;
	if (ent->health != health_before || ent->health <= 0 || ent->deadflag ||
	    ent->movetype != MOVETYPE_WALK ||
	    ent->client->ps.pmove.pm_type != PM_NORMAL ||
	    (ent->client->ps.pmove.pm_flags & PMF_TIME_TELEPORT) ||
	    ent->waterlevel >= 2 || !ent->groundentity)
		goto reject_pose;
	SG_AcceptDropResetLifeActions(bot);
	bot->was_dead = 0;
	bot->death_taught = false;
	bot->seed = selector->source;
	bot->prev_seed = -1;
	bot->commit_link = link;
	bot->sticky_link = link;
	VectorCopy(ent->s.origin, bot->last_origin);
	VectorCopy(ent->s.origin, bot->stuck_origin);
	VectorCopy(ent->s.origin, bot->watch_org);
	VectorCopy(ent->s.origin, bot->stag_org);
	VectorCopy(ent->s.origin, bot->wedge_org);
	for (axis = 0; axis < SG_BL_MAX; axis++)
		if (bot->bl_link[axis] == link)
		{
			bot->bl_link[axis] = -1;
			bot->bl_until[axis] = 0.0f;
		}
	SG_TimerArm(&bot->commit_until, 4.5f);
	accept_drop.armed = true;
	accept_drop.phase = SGAD_ACTIVE;
	AcceptLogArmed(selector, bot, link);
	SG_AcceptDropPose(bot, link, 0, ent);
	return;

reject_pose:
	accept_drop.phase = SGAD_FAILED;
	accept_drop.finished = true;
	accept_drop.final_reason = SG_REPLAY_REASON_INVALID_STATE;
	AcceptSummary("pose-rejected");
}

static const char *AcceptInjectedFinishFailure(
	const sg_accept_drop_state_t *state,
	const sg_accept_drop_selector_t *selector, qboolean legacy_variant)
{
	sg_accept_drop_checkpoint_t expected;
	unsigned int expected_commands;
	unsigned int expected_boundaries;
	unsigned int expected_deferrals;
	int axis;

	if (!state || !selector || selector->fixture_kind == SGAD_FIXTURE_NONE)
		return "finish-invalid-expectation";
	expected = legacy_variant ? selector->legacy_checkpoint :
	                            selector->rev2_checkpoint;
	if (expected == SGAD_CHECKPOINT_NONE || state->checkpoint != expected)
		return "finish-injection-checkpoint";
	expected_deferrals = AcceptLateAirborneSelector(selector) ?
	    state->injection_deferrals : 0;
	if (state->injection_deferral_events != expected_deferrals ||
	    state->injection_deferral_order_errors != 0 ||
	    (expected_deferrals == 0 ?
	         state->injection_deferral_last_ordinal != 0 :
	         state->injection_deferral_last_ordinal !=
	             expected_deferrals * SG_DROP_LIVE_FRAME_STEPS))
		return "finish-injection-count";
	if (!state->injection_pre_contact_captured ||
	    state->pre_contact_boundary_open || state->pre_contact_errors != 0 ||
	    state->pre_contact_validated != expected_deferrals ||
	    state->pre_contact_sampled != state->injection_pre_arrival_samples ||
	    (state->injection_pre_arrival_samples == 0 ?
	         state->pre_contact_last_sampled != 0 :
	         state->pre_contact_last_sampled != expected_deferrals) ||
	    (legacy_variant || !AcceptLateAirborneSelector(selector) ?
	         state->injection_pre_arrival_samples != 0 :
	         state->injection_pre_arrival_samples > 1))
		return "finish-contact-history";
	if (state->injection_attempts != 1 || state->injection_applied != 1 ||
	    state->injection_zero_ms != 1 || state->injection_errors != 0 ||
	    state->injection_step != selector->injection_step ||
	    state->injection_fixture_seed != selector->fixture_seed)
		return "finish-injection-count";
	for (axis = 0; axis < 3; axis++)
		if (state->injection_origin_bits[axis] !=
		    AcceptFixturePmoveBits(selector->fixture_bits[axis]))
			return "finish-injection-fixture";
	if (state->injection_pmove_traces == 0 ||
	    state->injection_pointcontents == 0 ||
	    (selector->fixture_kind == SGAD_FIXTURE_DRY_SUPPORTED &&
	     (!state->injection_grounded || !state->injection_support_valid)) ||
	    (selector->fixture_kind == SGAD_FIXTURE_WATER_DEPTH2 &&
	     state->injection_grounded && !state->injection_support_valid) ||
	    state->injection_terminal_geometry != selector->terminal_geometry ||
	    state->injection_recovery_geometry != selector->recovery_geometry ||
	    state->injection_health <= 0 || state->injection_deadflag != 0 ||
	    state->injection_movetype != MOVETYPE_WALK ||
	    !state->injection_oldvelocity_zero ||
	    (selector->fixture_kind == SGAD_FIXTURE_DRY_SUPPORTED &&
	     state->injection_waterlevel != 0) ||
	    (selector->fixture_kind == SGAD_FIXTURE_WATER_DEPTH2 &&
	     (state->injection_waterlevel < 2 ||
	      !(state->injection_watertype & CONTENTS_WATER))) ||
	    (state->injection_watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		return "finish-injection-fixture";
	if (selector->fixture_boundary == SGAD_FIXTURE_BOUNDARY_POST_COMMAND)
	{
		const sg_accept_drop_post_command_snapshot_t *snapshot =
		    &state->post_command;
		unsigned int post_commands =
		    (unsigned int)selector->injection_step + 2U;
		int axis;

		if (state->injection_post_command_captures != 1 ||
		    state->injection_post_command_validations != 1 ||
		    !snapshot->captured ||
		    snapshot->frame != state->injection_frame ||
		    snapshot->step != selector->injection_step + 1 ||
		    snapshot->historical_commands != post_commands ||
		    snapshot->commands != post_commands ||
		    snapshot->poses != post_commands ||
		    snapshot->final_historical_matches != post_commands ||
		    snapshot->final_historical_mismatches != 0 ||
		    snapshot->historical_pending)
			return "finish-injection-order";
		if (snapshot->pmove_type != PM_NORMAL ||
		    (snapshot->pmove_flags & PMF_TIME_TELEPORT) ||
		    snapshot->health <= 0 || snapshot->deadflag != 0 ||
		    snapshot->movetype != MOVETYPE_WALK || !snapshot->grounded ||
		    !snapshot->support_valid || snapshot->waterlevel != 0 ||
		    (snapshot->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) ||
		    snapshot->terminal_geometry != selector->terminal_geometry ||
		    snapshot->recovery_geometry != selector->recovery_geometry)
			return "finish-injection-fixture";
		for (axis = 0; axis < 3; axis++)
			if (snapshot->origin_bits[axis] == UINT32_C(0) ||
			    snapshot->oldvelocity_bits[axis] != UINT32_C(0))
				return "finish-injection-fixture";
	}
	else if (selector->fixture_boundary == SGAD_FIXTURE_BOUNDARY_EXACT_SEED)
	{
		if (state->injection_post_command_captures != 0 ||
		    state->injection_post_command_validations != 0 ||
		    state->post_command.captured)
			return "finish-injection-order";
	}
	else
		return "finish-invalid-expectation";
	if (!state->injection_pre_walkoff ||
	    state->injection_pre_airborne != selector->required_airborne ||
	    state->injection_pre_recovery)
		return "finish-injection-precondition";
	if (state->injection_frame < 0 ||
	    state->checkpoint_frame != state->injection_frame + 1 ||
	    state->injection_order_stage != SGAD_ORDER_CHECKPOINT ||
	    state->injection_order_errors != 0 ||
	    state->injection_entity_passes != 1 ||
	    state->injection_sg_frames != 1 ||
	    state->injection_boundary_checks != 1)
		return "finish-injection-order";
	if (!state->started || state->action_begins != 1 ||
	    state->action_begin_errors != 0)
		return "finish-action-count";
	expected_commands = AcceptLateAirborneSelector(selector) ?
	    (expected_deferrals + 1U) * SG_DROP_LIVE_FRAME_STEPS :
	    (expected == SGAD_CHECKPOINT_REV2_RUNNING ? 5U : 4U);
	expected_boundaries = expected_deferrals + 1U;
	if (expected_commands * SG_REPLAY_STEP_MS >
	        (unsigned int)selector->cost_ms)
		return "finish-injection-count";
	if (state->historical_commands != expected_commands ||
	    state->commands != expected_commands ||
	    state->poses != expected_commands || state->arm_poses != 1 ||
	    state->historical_pending || state->zero_final_commands != 0)
		return "finish-command-count";
	if (state->pusher_depth != 0 ||
	    state->pusher_begins != state->pusher_ends ||
	    state->pusher_order_errors != 0 ||
	    state->pusher_begins != (expected_boundaries + 1U) * 10U)
		return "finish-pusher-order";
	if (state->last_arrival || state->arrival_predicate_true != 0 ||
	    state->arrival_trace_true != 0 || state->result_arrivals != 0)
		return "finish-arrival-evidence";
	if (state->arrival_predicates != state->arrival_predicate_results ||
	    state->recovery_predicates != state->recovery_predicate_results ||
	    state->arrival_traces > state->arrival_predicates ||
	    state->recovery_traces > state->recovery_predicates ||
	    state->arrival_predicate_true != state->arrival_trace_true ||
	    state->recovery_predicate_true != state->recovery_trace_true)
		return "finish-contact-history";
	if (state->boundary_capture_open ||
	    state->boundary_enters != expected_boundaries)
		return "finish-boundary-count";
	if (state->private_stops != 1)
		return "finish-private-stop";
	{
		unsigned int expected_generic =
		    (expected == SGAD_CHECKPOINT_LEGACY_SHORT_CONTACT ||
		     expected == SGAD_CHECKPOINT_REV2_SHORT_LANDING) ? 1U : 0U;

		if (state->generic_handoffs != expected_generic ||
		    state->generic_handoff_begins != expected_generic ||
		    state->generic_handoff_ends != expected_generic ||
		    state->generic_handoff_completed_substeps !=
		        expected_generic *
		            SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS ||
		    state->generic_handoff_pending ||
		    state->generic_handoff_begin_valid != (qboolean)expected_generic ||
		    state->generic_handoff_substeps !=
		        (int)(expected_generic *
		            SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS) ||
		    state->generic_handoff_total_msec !=
		        (int)(expected_generic * SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC))
			return "finish-injection-checkpoint";
	}

	if (legacy_variant)
	{
		if (state->historical_commands != expected_commands ||
		    state->commands != expected_commands ||
		    state->final_historical_matches != expected_commands ||
		    state->final_historical_mismatches != 0 ||
		    !state->observer_began ||
		    state->observer_presteps != expected_commands ||
		    state->observer_poststeps != expected_commands ||
		    state->observer_boundaries != expected_boundaries ||
		    state->observer_command_matches != expected_commands ||
		    state->observer_command_mismatches != 0 ||
		    state->boundary_exits != 0 || state->boundary_results != 0 ||
		    state->arrival_callbacks != 0 || state->recovery_callbacks != 0)
			return "finish-observer-command";
		if (state->observer.progress.elapsed_ms !=
		        (int)(expected_commands * SG_REPLAY_STEP_MS) ||
		    state->observer.progress.arrival_ms != SG_REPLAY_TIME_DISCOVER ||
		    state->observer.recovery || state->observer_recovery_start_ms != 0)
			return "finish-observer-terminal";
		if (expected == SGAD_CHECKPOINT_LEGACY_WET_RECOVERY)
		{
			if (!state->saw_walkoff || !state->saw_airborne ||
			    !state->saw_recovery || state->handoffs != 3 ||
			    state->shelves != 0 || state->teaches != 0 ||
			    state->arrival_predicates != 1 || state->arrival_traces != 0 ||
			    state->recovery_predicates != 1 ||
			    state->recovery_predicate_true != 1 ||
			    state->recovery_traces != 1 || !state->last_recovery ||
			    state->legacy_recovery_start_ms !=
			        (int)(expected_commands * SG_REPLAY_STEP_MS) ||
			    state->observer_active ||
			    state->observer.progress.status != SG_REPLAY_FAILED ||
			    state->observer.progress.reason !=
			        SG_REPLAY_REASON_SHORT_LANDING ||
			    state->observer_arrival_cached != 1 ||
			    state->observer_recovery_cached != 1 ||
			    state->observer_recovery_cached_true != 1 ||
			    state->observer_arrival_inferred != expected_deferrals ||
			    state->observer_recovery_inferred != expected_deferrals ||
			    state->generic_handoffs != 0)
				return "finish-injection-checkpoint";
		}
		else if (selector->required_airborne)
		{
			if (!state->saw_walkoff || !state->saw_airborne ||
			    state->saw_recovery || state->handoffs != 2 ||
			    state->shelves != 1 || state->teaches != 0 ||
			    state->arrival_predicates != 1 || state->arrival_traces != 0 ||
			    state->recovery_predicates != 1 ||
			    state->recovery_predicate_true != 0 ||
			    state->recovery_traces != 0 || state->last_recovery ||
			    state->observer_active ||
			    state->observer.progress.status != SG_REPLAY_FAILED ||
			    state->observer.progress.reason !=
			        SG_REPLAY_REASON_SHORT_LANDING ||
			    state->observer_arrival_cached != 1 ||
			    state->observer_recovery_cached != 1 ||
			    state->observer_arrival_inferred != expected_deferrals ||
			    state->observer_recovery_inferred != expected_deferrals ||
			    state->generic_handoffs != 1)
				return "finish-injection-checkpoint";
		}
		else
		{
			if (!state->saw_walkoff || state->saw_airborne ||
			    state->saw_recovery || state->handoffs != 1 ||
			    state->shelves != 1 || state->teaches != 0 ||
			    state->arrival_predicates != 1 || state->arrival_traces != 0 ||
			    state->recovery_predicates != 0 ||
			    state->recovery_traces != 0 || state->last_recovery ||
			    !state->observer_active ||
			    state->observer.progress.status != SG_REPLAY_RUNNING ||
			    state->observer.progress.reason != SG_REPLAY_REASON_NONE ||
			    state->observer_arrival_cached != 1 ||
			    state->observer_recovery_inferred != 1 ||
			    state->observer_arrival_inferred != 0 ||
			    state->observer_recovery_cached != 0 ||
			    state->generic_handoffs != 1)
				return "finish-injection-checkpoint";
		}
	}
	else
	{
		if (state->observer_began || state->observer_active ||
		    state->observer_presteps != 0 || state->observer_poststeps != 0 ||
		    state->observer_boundaries != 0 ||
		    state->observer_command_matches != 0 ||
		    state->observer_command_mismatches != 0)
			return "finish-unexpected-observer";
		if (!state->saw_walkoff || state->saw_airborne !=
		        selector->required_airborne || state->saw_recovery ||
		    state->handoffs != (selector->required_airborne ? 2U : 1U) ||
		    state->teaches != 0 ||
		    state->boundary_exits != expected_boundaries ||
		    state->boundary_results != expected_boundaries ||
		    state->result_recovery_samples != 0 ||
		    state->result_recovery_ready != 0 ||
		    state->result_recovery_started != 0 ||
		    state->recovery_callbacks != 0 || state->recovery_predicates != 0 ||
		    state->recovery_traces != 0 || state->production_arrival_ms !=
		        SG_REPLAY_TIME_DISCOVER ||
		    state->production_recovery_start_ms != 0)
			return "finish-production-contact";
		if (expected == SGAD_CHECKPOINT_REV2_RUNNING)
		{
			if (state->shelves != 0 || state->arrival_callbacks != 0 ||
			    state->arrival_predicates != 0 || state->arrival_traces != 0 ||
			    state->result_arrival_samples != 0 ||
			    state->production_status != SG_REPLAY_RUNNING ||
			    state->production_reason != SG_REPLAY_REASON_NONE ||
			    state->production_elapsed_ms != 125 ||
			    state->final_outcome != SG_DROP_LIVE_RUNNING ||
			    state->final_reason != SG_REPLAY_REASON_NONE ||
			    state->generic_handoffs != 0)
				return "finish-production-terminal";
		}
		else
		{
			if (state->shelves != 1 ||
			    state->arrival_callbacks !=
			        state->injection_pre_arrival_samples + 1U ||
			    state->arrival_predicates !=
			        state->injection_pre_arrival_samples + 1U ||
			    state->arrival_predicate_results !=
			        state->injection_pre_arrival_samples + 1U ||
			    state->arrival_traces != 0 ||
			    state->result_arrival_samples !=
			        state->injection_pre_arrival_samples + 1U ||
			    state->production_status != SG_REPLAY_FAILED ||
			    state->production_reason != SG_REPLAY_REASON_SHORT_LANDING ||
			    state->production_elapsed_ms !=
			        (int)(expected_commands * SG_REPLAY_STEP_MS) ||
			    state->final_outcome != SG_DROP_LIVE_FAILED ||
			    state->final_reason != SG_REPLAY_REASON_SHORT_LANDING ||
			    state->generic_handoffs != 1)
				return "finish-production-terminal";
		}
	}
	return NULL;
}

/* Pure, fail-closed terminal oracle.  Keeping this independent of game state
 * makes every acceptance rule directly executable in the host-free tests. */
static const char *AcceptFinishFailure(const sg_accept_drop_state_t *state,
	const sg_accept_drop_selector_t *selector, qboolean legacy_variant)
{
	unsigned int expected_steps;
	unsigned int expected_boundaries;
	unsigned int expected_handoffs;

	if (!state || !selector || selector->cost_ms <= 0 ||
	    (selector->cost_ms % SG_REPLAY_STEP_MS) != 0 ||
	    (selector->cost_ms % SG_REPLAY_FRAME_MS) != 0 ||
	    (selector->recovery_required &&
	     (selector->recovery_start_ms <= 0 ||
	      selector->recovery_start_ms >= selector->cost_ms ||
	      (selector->recovery_start_ms % SG_REPLAY_FRAME_MS) != 0)) ||
	    (!selector->recovery_required && selector->recovery_start_ms != 0))
		return "finish-invalid-expectation";
	expected_steps = (unsigned int)(selector->cost_ms / SG_REPLAY_STEP_MS);
	expected_boundaries =
	    (unsigned int)(selector->cost_ms / SG_REPLAY_FRAME_MS);
	expected_handoffs = selector->recovery_required ? 3U : 2U;

	if (!state->started || state->action_begins != 1 ||
	    state->action_begin_errors != 0)
		return "finish-action-count";
	if (state->historical_commands != expected_steps ||
	    state->commands != expected_steps)
		return "finish-command-count";
	if (state->zero_final_commands != 0)
		return "finish-final-command-zeroed";
	if (state->arm_poses != 1 || state->poses != expected_steps)
		return "finish-pose-count";
	if (state->pusher_depth != 0 ||
	    state->pusher_begins != state->pusher_ends ||
	    state->pusher_order_errors != 0)
		return "finish-pusher-order";
	if (state->shelves != 0 || state->teaches != 0)
		return "finish-shelf-teach";
	if (!state->saw_walkoff || !state->saw_airborne ||
	    state->handoffs != expected_handoffs)
		return "finish-handoff-evidence";
	if (selector->recovery_required)
	{
		if (!state->saw_recovery || !state->last_recovery ||
		    state->recovery_predicate_true == 0 ||
		    state->recovery_trace_true == 0)
			return "finish-recovery-evidence";
	}
	else if (state->saw_recovery || state->last_recovery ||
	         state->recovery_callbacks != 0 ||
	         state->recovery_predicates != 0 ||
	         state->recovery_predicate_results != 0 ||
	         state->recovery_predicate_true != 0 ||
	         state->recovery_traces != 0 || state->recovery_trace_true != 0 ||
	         state->result_recovery_samples != 0 ||
	         state->result_recovery_ready != 0 ||
	         state->result_recovery_started != 0 ||
	         state->observer_recovery_cached_true != 0 ||
	         state->production_recovery_start_ms != 0 ||
	         state->observer_recovery_start_ms != 0 ||
	         state->legacy_recovery_start_ms != 0)
		return "finish-unexpected-recovery";
	if (!state->last_arrival || state->arrival_predicate_true != 1 ||
	    state->arrival_trace_true != 1)
		return "finish-arrival-evidence";
	if (state->arrival_predicates != state->arrival_predicate_results ||
	    state->recovery_predicates != state->recovery_predicate_results ||
	    state->arrival_traces > state->arrival_predicates ||
	    state->recovery_traces > state->recovery_predicates ||
	    state->arrival_predicate_true != state->arrival_trace_true ||
	    state->recovery_predicate_true != state->recovery_trace_true)
		return "finish-contact-history";
	if (state->boundary_capture_open ||
	    state->boundary_enters != expected_boundaries)
		return "finish-boundary-count";

	if (legacy_variant)
	{
		if (!state->observer_began || state->observer_active ||
		    state->observer.progress.status != SG_REPLAY_ARRIVED ||
		    state->observer.progress.reason != SG_REPLAY_REASON_NONE ||
		    state->observer.progress.elapsed_ms != selector->cost_ms ||
		    state->observer.progress.arrival_ms != selector->cost_ms)
			return "finish-observer-terminal";
		if (state->observer_presteps != expected_steps ||
		    state->observer_poststeps != expected_steps ||
		    state->observer_command_matches != expected_steps ||
		    state->observer_command_mismatches != 0 ||
		    state->historical_pending ||
		    state->final_historical_matches != expected_steps ||
		    state->final_historical_mismatches != 0)
			return "finish-observer-command";
		if (state->observer_boundaries != expected_boundaries ||
		    state->boundary_exits != 0 || state->boundary_results != 0)
			return "finish-observer-boundary";
		if (state->arrival_callbacks != 0 || state->recovery_callbacks != 0 ||
		    state->result_arrival_samples != 0 || state->result_arrivals != 0 ||
		    state->result_recovery_samples != 0 ||
		    state->result_recovery_ready != 0 ||
		    state->result_recovery_started != 0)
			return "finish-observer-host-call";
		if (state->observer_arrival_cached +
		        state->observer_arrival_inferred != expected_boundaries ||
		    state->observer_recovery_cached +
		        state->observer_recovery_inferred != expected_boundaries ||
		    state->observer_arrival_cached !=
		        state->arrival_predicate_results ||
		    state->observer_recovery_cached !=
		        state->recovery_predicate_results ||
		    state->observer_arrival_cached_true !=
		        state->arrival_predicate_true ||
		    state->observer_recovery_cached_true !=
		        state->recovery_predicate_true)
			return "finish-observer-contact-cache";
		if (selector->recovery_required &&
		    (!state->observer.recovery ||
		     state->observer_recovery_start_ms != selector->recovery_start_ms ||
		     state->legacy_recovery_start_ms != selector->recovery_start_ms))
			return "finish-recovery-timing";
		if (!selector->recovery_required && state->observer.recovery)
			return "finish-unexpected-recovery";
	}
	else
	{
		if (state->final_outcome != SG_DROP_LIVE_ARRIVED ||
		    state->final_reason != SG_REPLAY_REASON_NONE ||
		    state->production_status != SG_REPLAY_ARRIVED ||
		    state->production_reason != SG_REPLAY_REASON_NONE ||
		    state->production_elapsed_ms != selector->cost_ms ||
		    state->production_arrival_ms != selector->cost_ms)
			return "finish-production-terminal";
		if (state->boundary_exits != expected_boundaries ||
		    state->boundary_results != expected_boundaries)
			return "finish-production-boundary";
		if (state->arrival_callbacks != state->arrival_predicates ||
		    state->recovery_callbacks != state->recovery_predicates ||
		    state->result_arrival_samples != state->arrival_callbacks ||
		    state->result_recovery_samples != state->recovery_callbacks ||
		    state->result_arrivals != state->arrival_predicate_true ||
		    state->result_recovery_ready != state->recovery_predicate_true)
			return "finish-production-contact";
		if (state->result_arrivals != 1 ||
		    state->result_recovery_started !=
		        (selector->recovery_required ? 1U : 0U))
			return "finish-production-result";
		if (selector->recovery_required &&
		    state->production_recovery_start_ms != selector->recovery_start_ms)
			return "finish-recovery-timing";
		if (state->observer_began || state->observer_active ||
		    state->observer_presteps != 0 || state->observer_poststeps != 0 ||
		    state->observer_boundaries != 0 ||
		    state->observer_command_matches != 0 ||
		    state->observer_command_mismatches != 0 ||
		    state->observer_arrival_cached != 0 ||
		    state->observer_arrival_inferred != 0 ||
		    state->observer_recovery_cached != 0 ||
		    state->observer_recovery_inferred != 0)
			return "finish-unexpected-observer";
	}
	return NULL;
}

void SG_AcceptDropAfterBot(struct sg_bot_s *bot)
{
	const sg_accept_drop_selector_t *selector;
	const char *failure;

	if (!AcceptOwns(bot, accept_drop.link))
		return;
	selector = &accept_selectors[accept_drop.requested_case - 1];
	if (bot->drop_walkoff && !accept_drop.saw_walkoff)
	{
		accept_drop.saw_walkoff = true;
		accept_drop.handoffs++;
		AcceptLogPrefix("handoff-walkoff");
		sg_host.dprint(" link=%d\n", accept_drop.link);
	}
	if (bot->drop_airborne && !accept_drop.saw_airborne)
	{
		accept_drop.saw_airborne = true;
		accept_drop.handoffs++;
		AcceptLogPrefix("handoff-airborne");
		sg_host.dprint(" link=%d\n", accept_drop.link);
	}
	if (bot->drop_recover && !accept_drop.saw_recovery)
	{
		accept_drop.saw_recovery = true;
		accept_drop.handoffs++;
		AcceptLogPrefix("handoff-recovery");
		sg_host.dprint(" link=%d\n", accept_drop.link);
	}
	if (selector->fixture_kind != SGAD_FIXTURE_NONE &&
	    accept_drop.checkpoint != SGAD_CHECKPOINT_NONE)
	{
		AcceptCompleteInjected(bot, "after-bot-checkpoint");
		return;
	}
	if (accept_drop.armed && bot->commit_link != accept_drop.link)
	{
		failure = AcceptFinishFailure(&accept_drop, selector,
		    SG_ACCEPT_DROP_LEGACY_A);
		accept_drop.finished = true;
		if (failure)
		{
			accept_drop.phase = SGAD_FAILED;
			accept_drop.final_outcome = SG_DROP_LIVE_FALLBACK;
			accept_drop.final_reason = SG_REPLAY_REASON_INVALID_STATE;
			accept_drop.finish_diagnostic = failure;
			AcceptSummary("acceptance-rejected");
		}
		else
		{
			accept_drop.phase = SGAD_FINISHED;
			accept_drop.final_outcome = SG_DROP_LIVE_ARRIVED;
			accept_drop.final_reason = SG_REPLAY_REASON_NONE;
			accept_drop.finish_diagnostic = "none";
			AcceptSummary("arrived");
		}
	}
}

qboolean SG_AcceptDropLegacyAuthority(const struct sg_bot_s *bot,
	int link_index)
{
	return SG_ACCEPT_DROP_LEGACY_A && AcceptOwns(bot, link_index);
}

qboolean SG_AcceptDropOwnsStep(const struct sg_bot_s *bot, int link_index)
{
	return AcceptOwns(bot, link_index) && accept_drop.started &&
	       bot->commit_link == link_index;
}

qboolean SG_AcceptDropObserverEventOwner(const struct sg_bot_s *bot)
{
	return SG_ACCEPT_DROP_LEGACY_A && AcceptOwns(bot, accept_drop.link) &&
	       accept_drop.started && accept_drop.observer_active &&
	       bot->commit_link == accept_drop.link && !bot->drop_replay_active;
}

qboolean SG_AcceptDropObserverBeginCommand(const struct sg_bot_s *bot,
	int link_index, sg_drop_live_events_t *events,
	qboolean *source_door_pending)
{
	if (!SG_AcceptDropObserverEventOwner(bot) || link_index != accept_drop.link ||
	    !events || accept_drop.observer_events_pending)
		return false;
	/* Match production's clear-before-observe boundary.  In particular, the
	 * source-preflight door bit is installed only on the first real 25 ms
	 * command, after stale source events have been discarded. */
	return SG_DropLiveEventsBeginCommand(events, source_door_pending);
}

void SG_AcceptDropObserverTakeEvents(const struct sg_bot_s *bot,
	int link_index, int step, sg_drop_live_events_t *events)
{
	if (!SG_AcceptDropObserverEventOwner(bot) || link_index != accept_drop.link ||
	    !events || accept_drop.observer_events_pending)
		return;
	accept_drop.observer_events = *events;
	memset(events, 0, sizeof(*events));
	accept_drop.observer_events_pending = true;
	accept_drop.observer_events_step = step;
}

/* Move exactly one host-owned event snapshot into the pure observer's
 * post-command observation.  Missing or out-of-order feed evidence is
 * intentionally contamination: A remains production-legacy, but its witness
 * cannot silently prove a command whose host event record was lost. */
static qboolean AcceptObserverEventsApply(int step,
	sg_replay_observation_t *observation)
{
	if (!observation || !accept_drop.observer_events_pending ||
	    accept_drop.observer_events_step != step)
	{
		if (observation)
			observation->contaminated = true;
		return false;
	}
	observation->contaminated = accept_drop.observer_events.contaminated;
	observation->door_passed = accept_drop.observer_events.door_passed;
	memset(&accept_drop.observer_events, 0,
	       sizeof(accept_drop.observer_events));
	accept_drop.observer_events_pending = false;
	accept_drop.observer_events_step = -1;
	return true;
}

static void AcceptPoseFromEnt(const edict_t *ent, sg_replay_pose_t *pose)
{
	SG_DropLivePose(pose,
	    ent && ent->client ? &ent->client->ps.pmove : NULL,
	    ent ? ent->s.origin : NULL, ent ? ent->velocity : NULL,
	    ent && ent->groundentity != NULL, ent ? ent->watertype : 0,
	    ent ? ent->waterlevel : 0);
}

static qboolean AcceptSupportValid(const edict_t *ent)
{
	return ent && ent->groundentity &&
	       (ent->groundentity == g_edicts ||
	        SG_ImmutableSupport(ent->groundentity));
}

static sg_replay_observation_t AcceptObservation(const edict_t *ent)
{
	sg_replay_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.ground_support_valid = AcceptSupportValid(ent);
	return observation;
}

static edict_t *accept_fixture_passent;

static trace_t AcceptFixtureTrace(vec3_t start, vec3_t mins,
	vec3_t maxs, vec3_t end)
{
	accept_drop.injection_pmove_traces++;
	return sg_host.trace(start, mins, maxs, end, accept_fixture_passent,
	    accept_fixture_passent && accept_fixture_passent->health > 0 ?
	        MASK_PLAYERSOLID : MASK_DEADSOLID);
}

static int AcceptFixturePointContents(vec3_t point)
{
	accept_drop.injection_pointcontents++;
	return sg_host.pointcontents(point);
}

static qboolean AcceptFixtureGeometry(const edict_t *ent,
	const sg_accept_drop_selector_t *selector, qboolean *terminal_geometry,
	qboolean *recovery_geometry)
{
	const rune_t *rune = SG_Rune();

	if (!ent || !selector || !rune || !terminal_geometry ||
	    !recovery_geometry || selector->destination < 0 ||
	    selector->destination >= rune->hdr.num_seeds)
		return false;
	*terminal_geometry = AcceptWithinGeometry(ent->s.origin,
	    rune->seeds[selector->destination].origin,
	    SG_REPLAY_ARRIVE_RADIUS, SG_REPLAY_ARRIVE_Z);
	*recovery_geometry = AcceptWithinGeometry(ent->s.origin,
	    rune->seeds[selector->destination].origin,
	    SG_RUNE_PROOF_DROP_RECOVERY_RADIUS,
	    SG_RUNE_PROOF_DROP_RECOVERY_Z);
	return true;
}

/* This is the placement-time proof.  It intentionally remains tied to the
 * authenticated fixture seed and zeroed Pmove state; it is never used after a
 * real selected command has consumed that fixture. */
static qboolean AcceptFixtureSeedSnapshotValid(const edict_t *ent,
	const sg_accept_drop_selector_t *selector)
{
	const rune_t *rune = SG_Rune();
	qboolean terminal_geometry;
	qboolean recovery_geometry;
	int axis;

	if (!ent || !ent->client || !selector || !rune ||
	    selector->fixture_kind == SGAD_FIXTURE_NONE ||
	    selector->fixture_seed < 0 ||
	    selector->fixture_seed >= rune->hdr.num_seeds ||
	    ent->health <= 0 || ent->deadflag || ent->movetype != MOVETYPE_WALK ||
	    ent->client->ps.pmove.pm_type != PM_NORMAL ||
	    ent->client->ps.pmove.gravity != (short)rune->v3_header.gravity ||
	    (ent->client->ps.pmove.pm_flags & PMF_TIME_TELEPORT))
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		short fixed =
		    (short)(rune->seeds[selector->fixture_seed].origin[axis] * 8.0f);

		if (AcceptFloatBits(ent->s.origin[axis]) !=
		        AcceptFixturePmoveBits(selector->fixture_bits[axis]) ||
		    ent->client->ps.pmove.origin[axis] != fixed ||
		    ent->client->ps.pmove.velocity[axis] != 0 ||
		    AcceptFloatBits(ent->velocity[axis]) != UINT32_C(0) ||
		    AcceptFloatBits(ent->client->oldvelocity[axis]) != UINT32_C(0))
			return false;
	}
	if (ent->watertype & (CONTENTS_LAVA | CONTENTS_SLIME))
		return false;
	if (ent->groundentity && ent->groundentity != g_edicts &&
	    !SG_ImmutableSupport(ent->groundentity))
		return false;
	if (selector->fixture_kind == SGAD_FIXTURE_DRY_SUPPORTED)
	{
		if (ent->waterlevel != 0 || !AcceptSupportValid(ent))
			return false;
	}
	else if (selector->fixture_kind == SGAD_FIXTURE_WATER_DEPTH2)
	{
		if (ent->waterlevel < 2 || !(ent->watertype & CONTENTS_WATER))
			return false;
	}
	else
		return false;
	if (!AcceptFixtureGeometry(ent, selector, &terminal_geometry,
	        &recovery_geometry))
		return false;
	return terminal_geometry == selector->terminal_geometry &&
	       recovery_geometry == selector->recovery_geometry;
}

static qboolean AcceptFixtureExactBoundaryValid(const edict_t *ent,
	const sg_accept_drop_selector_t *selector)
{
	qboolean terminal_geometry;
	qboolean recovery_geometry;
	int axis;

	if (!AcceptFixtureSeedSnapshotValid(ent, selector) ||
	    accept_drop.injection_fixture_seed != selector->fixture_seed ||
	    ent->health != accept_drop.injection_health ||
	    ent->deadflag != accept_drop.injection_deadflag ||
	    ent->movetype != accept_drop.injection_movetype ||
	    (ent->groundentity != NULL) != accept_drop.injection_grounded ||
	    AcceptSupportValid(ent) != accept_drop.injection_support_valid ||
	    ent->watertype != accept_drop.injection_watertype ||
	    ent->waterlevel != accept_drop.injection_waterlevel ||
	    !accept_drop.injection_oldvelocity_zero ||
	    !AcceptFixtureGeometry(ent, selector, &terminal_geometry,
	        &recovery_geometry) ||
	    terminal_geometry != accept_drop.injection_terminal_geometry ||
	    recovery_geometry != accept_drop.injection_recovery_geometry)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (AcceptFloatBits(ent->s.origin[axis]) !=
		        accept_drop.injection_origin_bits[axis])
			return false;
	return true;
}

static qboolean AcceptPostCommandCaptureReady(const struct sg_bot_s *bot,
	const edict_t *ent, const sg_accept_drop_selector_t *selector)
{
	const rune_t *rune = SG_Rune();
	unsigned int expected_commands;
	qboolean terminal_geometry;
	qboolean recovery_geometry;

	if (!bot || !ent || !ent->client || !selector || !rune ||
	    selector->fixture_boundary != SGAD_FIXTURE_BOUNDARY_POST_COMMAND ||
	    selector->injection_step < 0 ||
	    accept_drop.injection_applied != 1 ||
	    accept_drop.injection_step != selector->injection_step ||
	    accept_drop.injection_fixture_seed != selector->fixture_seed ||
	    bot->commit_link != accept_drop.link || !bot->drop_started ||
	    !bot->drop_walkoff || bot->drop_airborne != selector->required_airborne ||
	    bot->drop_recover || ent->health <= 0 || ent->deadflag ||
	    ent->movetype != MOVETYPE_WALK ||
	    ent->client->ps.pmove.pm_type != PM_NORMAL ||
	    ent->client->ps.pmove.gravity != (short)rune->v3_header.gravity ||
	    (ent->client->ps.pmove.pm_flags & PMF_TIME_TELEPORT) ||
	    (ent->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) ||
	    ent->waterlevel != 0 || !AcceptSupportValid(ent) ||
	    !AcceptFixtureGeometry(ent, selector, &terminal_geometry,
	        &recovery_geometry) ||
	    terminal_geometry != selector->terminal_geometry ||
	    recovery_geometry != selector->recovery_geometry)
		return false;
	expected_commands = (unsigned int)selector->injection_step + 2U;
	if (accept_drop.historical_commands != expected_commands ||
	    accept_drop.commands != expected_commands ||
	    accept_drop.poses != expected_commands ||
	    accept_drop.final_historical_matches != expected_commands ||
	    accept_drop.final_historical_mismatches != 0 ||
	    accept_drop.historical_pending || accept_drop.zero_final_commands != 0)
		return false;
	if (SG_ACCEPT_DROP_LEGACY_A)
		return accept_drop.observer_began && accept_drop.observer_active &&
		       accept_drop.observer.progress.status == SG_REPLAY_RUNNING &&
		       accept_drop.observer.progress.reason == SG_REPLAY_REASON_NONE &&
		       accept_drop.observer.progress.elapsed_ms ==
		           (int)((expected_commands - 1U) * SG_REPLAY_STEP_MS) &&
		       accept_drop.observer.progress.step_pending &&
		       accept_drop.observer.walkoff &&
		       accept_drop.observer.airborne == selector->required_airborne &&
		       !accept_drop.observer.recovery &&
		       accept_drop.observer_presteps == expected_commands &&
		       accept_drop.observer_poststeps == expected_commands - 1U &&
		       accept_drop.observer_command_matches == expected_commands &&
		       accept_drop.observer_command_mismatches == 0;
	return bot->drop_replay_active &&
	       bot->drop_replay_link == accept_drop.link &&
	       bot->drop_replay.progress.status == SG_REPLAY_RUNNING &&
	       bot->drop_replay.progress.reason == SG_REPLAY_REASON_NONE &&
	       bot->drop_replay.progress.elapsed_ms ==
	           (int)((expected_commands - 1U) * SG_REPLAY_STEP_MS) &&
	       bot->drop_replay.progress.step_pending && bot->drop_replay.walkoff &&
	       bot->drop_replay.airborne == selector->required_airborne &&
	       !bot->drop_replay.recovery;
}

static qboolean AcceptCapturePostCommandFixture(struct sg_bot_s *bot,
	const edict_t *ent, const sg_accept_drop_selector_t *selector)
{
	sg_accept_drop_post_command_snapshot_t *snapshot =
	    &accept_drop.post_command;
	int axis;

	if (!AcceptPostCommandCaptureReady(bot, ent, selector) ||
	    accept_drop.injection_order_stage != SGAD_ORDER_INJECTED ||
	    accept_drop.injection_frame != level.framenum ||
	    snapshot->captured || accept_drop.injection_post_command_captures != 0)
		return false;
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->captured = true;
	snapshot->frame = level.framenum;
	snapshot->step = selector->injection_step + 1;
	for (axis = 0; axis < 3; axis++)
	{
		snapshot->origin_bits[axis] = AcceptFloatBits(ent->s.origin[axis]);
		snapshot->velocity_bits[axis] = AcceptFloatBits(ent->velocity[axis]);
		snapshot->old_origin_bits[axis] =
		    AcceptFloatBits(ent->s.old_origin[axis]);
		snapshot->oldvelocity_bits[axis] =
		    AcceptFloatBits(ent->client->oldvelocity[axis]);
		snapshot->pmove_origin[axis] = ent->client->ps.pmove.origin[axis];
		snapshot->pmove_velocity[axis] = ent->client->ps.pmove.velocity[axis];
	}
	snapshot->pmove_type = ent->client->ps.pmove.pm_type;
	snapshot->pmove_gravity = ent->client->ps.pmove.gravity;
	snapshot->pmove_flags = ent->client->ps.pmove.pm_flags;
	snapshot->health = ent->health;
	snapshot->deadflag = ent->deadflag;
	snapshot->movetype = ent->movetype;
	snapshot->grounded = ent->groundentity != NULL;
	snapshot->groundentity = ent->groundentity;
	snapshot->groundentity_linkcount = ent->groundentity_linkcount;
	snapshot->support_valid = AcceptSupportValid(ent);
	snapshot->watertype = ent->watertype;
	snapshot->waterlevel = ent->waterlevel;
	if (!AcceptFixtureGeometry(ent, selector, &snapshot->terminal_geometry,
	        &snapshot->recovery_geometry))
		return false;
	snapshot->historical_commands = accept_drop.historical_commands;
	snapshot->commands = accept_drop.commands;
	snapshot->poses = accept_drop.poses;
	snapshot->final_historical_matches = accept_drop.final_historical_matches;
	snapshot->final_historical_mismatches =
	    accept_drop.final_historical_mismatches;
	snapshot->historical_pending = accept_drop.historical_pending;
	snapshot->reducer_active = bot->drop_replay_active;
	snapshot->reducer_link = bot->drop_replay_link;
	snapshot->reducer_status = bot->drop_replay.progress.status;
	snapshot->reducer_reason = bot->drop_replay.progress.reason;
	snapshot->reducer_elapsed_ms = bot->drop_replay.progress.elapsed_ms;
	snapshot->reducer_arrival_ms = bot->drop_replay.progress.arrival_ms;
	snapshot->reducer_step_pending = bot->drop_replay.progress.step_pending;
	snapshot->reducer_walkoff = bot->drop_replay.walkoff;
	snapshot->reducer_airborne = bot->drop_replay.airborne;
	snapshot->reducer_recovery = bot->drop_replay.recovery;
	snapshot->observer_active = accept_drop.observer_active;
	snapshot->observer_status = accept_drop.observer.progress.status;
	snapshot->observer_reason = accept_drop.observer.progress.reason;
	snapshot->observer_elapsed_ms = accept_drop.observer.progress.elapsed_ms;
	snapshot->observer_arrival_ms = accept_drop.observer.progress.arrival_ms;
	snapshot->observer_step_pending = accept_drop.observer.progress.step_pending;
	snapshot->observer_walkoff = accept_drop.observer.walkoff;
	snapshot->observer_airborne = accept_drop.observer.airborne;
	snapshot->observer_recovery = accept_drop.observer.recovery;
	snapshot->observer_presteps = accept_drop.observer_presteps;
	snapshot->observer_poststeps = accept_drop.observer_poststeps;
	snapshot->observer_boundaries = accept_drop.observer_boundaries;
	snapshot->observer_command_matches = accept_drop.observer_command_matches;
	snapshot->observer_command_mismatches =
	    accept_drop.observer_command_mismatches;
	accept_drop.injection_post_command_captures++;
	return true;
}

static const char *AcceptSnapshotMismatchFirst(uint64_t mismatch)
{
	static const char *const tokens[SGAD_SNAPSHOT_BIT_COUNT] = {
		"selector", "entity", "client", "bot", "captured",
		"capture_count", "frame", "step", "pmove_type",
		"pmove_gravity", "pmove_flags", "health", "deadflag",
		"movetype", "grounded", "groundentity", "ground_linkcount",
		"support", "watertype", "waterlevel", "historical_commands",
		"commands", "poses", "final_matches", "final_mismatches",
		"historical_pending", "reducer_active", "reducer_link",
		"reducer_status", "reducer_reason", "reducer_elapsed",
		"reducer_arrival", "reducer_pending", "reducer_walkoff",
		"reducer_airborne", "reducer_recovery", "observer_active",
		"observer_status", "observer_reason", "observer_elapsed",
		"observer_arrival", "observer_pending", "observer_walkoff",
		"observer_airborne", "observer_recovery", "observer_presteps",
		"observer_poststeps", "observer_boundaries", "observer_matches",
		"observer_mismatches", "origin", "velocity", "old_origin",
		"oldvelocity", "pmove_origin", "pmove_velocity", "geometry_query",
		"terminal_geometry", "recovery_geometry", "capture_ready",
		"fixture_boundary"
	};
	unsigned int bit;

	_Static_assert(SGAD_SNAPSHOT_BIT_COUNT <= 64,
	    "snapshot mismatch mask exceeds uint64_t");
	_Static_assert(sizeof(tokens) / sizeof(tokens[0]) ==
	    SGAD_SNAPSHOT_BIT_COUNT,
	    "snapshot mismatch tokens must cover every bit");
	if (!mismatch)
		return "none";
	for (bit = 0; bit < SGAD_SNAPSHOT_BIT_COUNT; bit++)
		if (mismatch & SGAD_SNAPSHOT_MASK(bit))
			return tokens[bit];
	return "unknown";
}

static uint64_t AcceptPostCommandFixtureSnapshotMismatch(const edict_t *ent,
	const sg_accept_drop_selector_t *selector)
{
	const sg_accept_drop_post_command_snapshot_t *snapshot =
	    &accept_drop.post_command;
	const struct sg_bot_s *bot = accept_drop.bot;
	uint64_t mismatch = 0;
	qboolean terminal_geometry;
	qboolean recovery_geometry;
	int axis;

	if (!selector)
		return SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_SELECTOR);
	if (!ent)
		return SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_ENTITY);
	if (!ent->client)
		return SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_CLIENT);
	if (!bot)
		return SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_BOT);
	if (selector->fixture_boundary != SGAD_FIXTURE_BOUNDARY_POST_COMMAND)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_FIXTURE_BOUNDARY);
	if (!snapshot->captured)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_CAPTURED);
	if (accept_drop.injection_post_command_captures != 1)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_CAPTURE_COUNT);
	if (snapshot->frame != accept_drop.injection_frame)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_FRAME);
	if (snapshot->step != selector->injection_step + 1)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_STEP);
	if ((int)ent->client->ps.pmove.pm_type != snapshot->pmove_type)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_PMOVE_TYPE);
	if (ent->client->ps.pmove.gravity != snapshot->pmove_gravity)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_PMOVE_GRAVITY);
	if (ent->client->ps.pmove.pm_flags != snapshot->pmove_flags)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_PMOVE_FLAGS);
	if (ent->health != snapshot->health)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_HEALTH);
	if (ent->deadflag != snapshot->deadflag)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_DEADFLAG);
	if (ent->movetype != snapshot->movetype)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_MOVETYPE);
	if ((ent->groundentity != NULL) != snapshot->grounded)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_GROUNDED);
	if (ent->groundentity != snapshot->groundentity)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_GROUNDENTITY);
	if (ent->groundentity_linkcount != snapshot->groundentity_linkcount)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_GROUND_LINKCOUNT);
	if (AcceptSupportValid(ent) != snapshot->support_valid)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_SUPPORT);
	if (ent->watertype != snapshot->watertype)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_WATERTYPE);
	if (ent->waterlevel != snapshot->waterlevel)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_WATERLEVEL);
	if (accept_drop.historical_commands != snapshot->historical_commands)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_HISTORICAL_COMMANDS);
	if (accept_drop.commands != snapshot->commands)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_COMMANDS);
	if (accept_drop.poses != snapshot->poses)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_POSES);
	if (accept_drop.final_historical_matches !=
	    snapshot->final_historical_matches)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_FINAL_MATCHES);
	if (accept_drop.final_historical_mismatches !=
	    snapshot->final_historical_mismatches)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_FINAL_MISMATCHES);
	if (accept_drop.historical_pending != snapshot->historical_pending)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_HISTORICAL_PENDING);
	if (bot->drop_replay_active != snapshot->reducer_active)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_REDUCER_ACTIVE);
	if (bot->drop_replay_link != snapshot->reducer_link)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_REDUCER_LINK);
	if (bot->drop_replay.progress.status != snapshot->reducer_status)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_REDUCER_STATUS);
	if (bot->drop_replay.progress.reason != snapshot->reducer_reason)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_REDUCER_REASON);
	if (bot->drop_replay.progress.elapsed_ms != snapshot->reducer_elapsed_ms)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_REDUCER_ELAPSED);
	if (bot->drop_replay.progress.arrival_ms != snapshot->reducer_arrival_ms)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_REDUCER_ARRIVAL);
	if (bot->drop_replay.progress.step_pending != snapshot->reducer_step_pending)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_REDUCER_PENDING);
	if (bot->drop_replay.walkoff != snapshot->reducer_walkoff)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_REDUCER_WALKOFF);
	if (bot->drop_replay.airborne != snapshot->reducer_airborne)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_REDUCER_AIRBORNE);
	if (bot->drop_replay.recovery != snapshot->reducer_recovery)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_REDUCER_RECOVERY);
	if (accept_drop.observer_active != snapshot->observer_active)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_ACTIVE);
	if (accept_drop.observer.progress.status != snapshot->observer_status)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_STATUS);
	if (accept_drop.observer.progress.reason != snapshot->observer_reason)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_REASON);
	if (accept_drop.observer.progress.elapsed_ms != snapshot->observer_elapsed_ms)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_ELAPSED);
	if (accept_drop.observer.progress.arrival_ms != snapshot->observer_arrival_ms)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_ARRIVAL);
	if (accept_drop.observer.progress.step_pending !=
	    snapshot->observer_step_pending)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_PENDING);
	if (accept_drop.observer.walkoff != snapshot->observer_walkoff)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_WALKOFF);
	if (accept_drop.observer.airborne != snapshot->observer_airborne)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_AIRBORNE);
	if (accept_drop.observer.recovery != snapshot->observer_recovery)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_RECOVERY);
	if (accept_drop.observer_presteps != snapshot->observer_presteps)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_PRESTEPS);
	if (accept_drop.observer_poststeps != snapshot->observer_poststeps)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_POSTSTEPS);
	if (accept_drop.observer_boundaries != snapshot->observer_boundaries)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_BOUNDARIES);
	if (accept_drop.observer_command_matches !=
	    snapshot->observer_command_matches)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_MATCHES);
	if (accept_drop.observer_command_mismatches !=
	    snapshot->observer_command_mismatches)
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OBSERVER_MISMATCHES);
	for (axis = 0; axis < 3; axis++)
	{
		if (AcceptFloatBits(ent->s.origin[axis]) !=
		    snapshot->origin_bits[axis])
			mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_ORIGIN);
		if (AcceptFloatBits(ent->velocity[axis]) !=
		    snapshot->velocity_bits[axis])
			mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_VELOCITY);
		if (AcceptFloatBits(ent->s.old_origin[axis]) !=
		    ((ent->flags & FL_OLDORGNOTSET) ?
		        snapshot->old_origin_bits[axis] : snapshot->origin_bits[axis]))
			mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OLD_ORIGIN);
		/* ClientEndServerFrame runs after SG_RunFrame in command four's
		 * outer frame, so the next boundary must see oldvelocity rolled to
		 * the captured command-four velocity, not the captured zero seed. */
		if (AcceptFloatBits(ent->client->oldvelocity[axis]) !=
		    snapshot->velocity_bits[axis])
			mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OLDVELOCITY);
		if (ent->client->ps.pmove.origin[axis] !=
		    snapshot->pmove_origin[axis])
			mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_PMOVE_ORIGIN);
		if (ent->client->ps.pmove.velocity[axis] !=
		    snapshot->pmove_velocity[axis])
			mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_PMOVE_VELOCITY);
	}
	if (!AcceptFixtureGeometry(ent, selector, &terminal_geometry,
	    &recovery_geometry))
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_GEOMETRY_QUERY);
	else
	{
		if (terminal_geometry != snapshot->terminal_geometry)
			mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_TERMINAL_GEOMETRY);
		if (recovery_geometry != snapshot->recovery_geometry)
			mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_RECOVERY_GEOMETRY);
	}
	if (!AcceptPostCommandCaptureReady(bot, ent, selector))
		mismatch |= SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_CAPTURE_READY);
	return mismatch;
}

static uint64_t AcceptFixtureSnapshotMismatch(const edict_t *ent,
	const sg_accept_drop_selector_t *selector)
{
	if (!selector)
		return SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_SELECTOR);
	if (selector->fixture_boundary == SGAD_FIXTURE_BOUNDARY_EXACT_SEED)
		return AcceptFixtureExactBoundaryValid(ent, selector) ? 0 :
		    SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_FIXTURE_BOUNDARY);
	if (selector->fixture_boundary == SGAD_FIXTURE_BOUNDARY_POST_COMMAND)
		return AcceptPostCommandFixtureSnapshotMismatch(ent, selector);
	return SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_FIXTURE_BOUNDARY);
}

static qboolean AcceptFixtureSnapshotValid(const edict_t *ent,
	const sg_accept_drop_selector_t *selector)
{
	return AcceptFixtureSnapshotMismatch(ent, selector) == 0;
}

static qboolean AcceptLateAirborneSelector(
	const sg_accept_drop_selector_t *selector)
{
	return selector && selector->fixture_kind != SGAD_FIXTURE_NONE &&
	       selector->required_airborne && selector->injection_step == 3 &&
	       selector->fixture_boundary == SGAD_FIXTURE_BOUNDARY_EXACT_SEED;
}

static qboolean AcceptPreInjectionContactLaw(
	const sg_accept_drop_selector_t *selector, unsigned int expected_deferrals)
{
	qboolean late_airborne = AcceptLateAirborneSelector(selector);
	unsigned int sampled = accept_drop.result_arrival_samples;

	if (accept_drop.pre_contact_boundary_open ||
	    accept_drop.pre_contact_errors != 0 ||
	    accept_drop.pre_contact_validated != expected_deferrals ||
	    accept_drop.shelves != 0 || accept_drop.teaches != 0 ||
	    accept_drop.last_arrival || accept_drop.last_recovery)
		return false;
	if (!late_airborne)
		sampled = 0;
	if (SG_ACCEPT_DROP_LEGACY_A)
		return accept_drop.pre_contact_sampled == 0 &&
			       accept_drop.pre_contact_last_sampled == 0 &&
			       accept_drop.arrival_callbacks == 0 &&
			       accept_drop.recovery_callbacks == 0 &&
			       accept_drop.arrival_predicates == 0 &&
			       accept_drop.recovery_predicates == 0 &&
			       accept_drop.arrival_predicate_results == 0 &&
			       accept_drop.recovery_predicate_results == 0 &&
			       accept_drop.arrival_predicate_true == 0 &&
			       accept_drop.recovery_predicate_true == 0 &&
			       accept_drop.arrival_traces == 0 &&
			       accept_drop.recovery_traces == 0 &&
			       accept_drop.arrival_trace_true == 0 &&
			       accept_drop.recovery_trace_true == 0 &&
			       accept_drop.result_arrival_samples == 0 &&
			       accept_drop.result_arrivals == 0 &&
			       accept_drop.result_recovery_samples == 0 &&
			       accept_drop.result_recovery_ready == 0 &&
			       accept_drop.result_recovery_started == 0 &&
			       accept_drop.observer_arrival_cached == 0 &&
			       accept_drop.observer_arrival_inferred == expected_deferrals &&
			       accept_drop.observer_arrival_cached_true == 0 &&
			       accept_drop.observer_recovery_cached == 0 &&
			       accept_drop.observer_recovery_inferred == expected_deferrals &&
			       accept_drop.observer_recovery_cached_true == 0;
	return sampled <= (late_airborne ? 1U : 0U) &&
	       accept_drop.pre_contact_sampled == sampled &&
	       (sampled == 0 ? accept_drop.pre_contact_last_sampled == 0 :
	                       accept_drop.pre_contact_last_sampled ==
	                           expected_deferrals) &&
	       accept_drop.arrival_callbacks == sampled &&
	       accept_drop.recovery_callbacks == 0 &&
	       accept_drop.arrival_predicates == sampled &&
	       accept_drop.recovery_predicates == 0 &&
	       accept_drop.arrival_predicate_results == sampled &&
	       accept_drop.recovery_predicate_results == 0 &&
	       accept_drop.arrival_predicate_true == 0 &&
	       accept_drop.recovery_predicate_true == 0 &&
	       accept_drop.arrival_traces == 0 &&
	       accept_drop.recovery_traces == 0 &&
	       accept_drop.arrival_trace_true == 0 &&
	       accept_drop.recovery_trace_true == 0 &&
	       accept_drop.result_arrival_samples == sampled &&
	       accept_drop.result_arrivals == 0 &&
	       accept_drop.result_recovery_samples == 0 &&
	       accept_drop.result_recovery_ready == 0 &&
	       accept_drop.result_recovery_started == 0 &&
	       accept_drop.observer_arrival_cached == 0 &&
	       accept_drop.observer_arrival_inferred == 0 &&
	       accept_drop.observer_arrival_cached_true == 0 &&
	       accept_drop.observer_recovery_cached == 0 &&
	       accept_drop.observer_recovery_inferred == 0 &&
	       accept_drop.observer_recovery_cached_true == 0;
}

/* Authenticate a completed selected-command group without consuming or
 * fabricating the pending step-3 reducer sample.  At AfterStep(step=3), the
 * real legacy flags already include the submitted command, while revision 2
 * and the A observer deliberately retain that command for the next boundary.
 * Their exact pending clock proves cadence; the real post-command flags prove
 * the transition. */
static qboolean AcceptInjectionCadence(const struct sg_bot_s *bot,
	int link_index, int step, const sg_accept_drop_selector_t *selector,
	qboolean require_transition)
{
	unsigned int expected_commands;
	unsigned int expected_poststeps;
	unsigned int expected_deferrals;
	int expected_elapsed_ms;
	qboolean expected_pending;
	qboolean late_airborne;

	if (!bot || !selector || selector->fixture_kind == SGAD_FIXTURE_NONE ||
	    step != selector->injection_step || step < 0 || step > 3)
		return false;
	late_airborne = AcceptLateAirborneSelector(selector);
	if (late_airborne)
	{
		expected_commands = accept_drop.commands;
		if (expected_commands < SG_DROP_LIVE_FRAME_STEPS ||
		    (expected_commands % SG_DROP_LIVE_FRAME_STEPS) != 0 ||
		    expected_commands >
		        (unsigned int)(selector->cost_ms / SG_REPLAY_STEP_MS))
			return false;
		expected_deferrals =
		    expected_commands / SG_DROP_LIVE_FRAME_STEPS - 1U;
	}
	else
	{
		expected_commands = (unsigned int)step + 1U;
		expected_deferrals = 0;
	}
	expected_pending = step == 3;
	expected_poststeps = expected_commands - (expected_pending ? 1U : 0U);
	expected_elapsed_ms = (int)(expected_poststeps * SG_REPLAY_STEP_MS);
	if (bot->commit_link != link_index || !bot->drop_started ||
	    (require_transition &&
	     (!bot->drop_walkoff ||
	      bot->drop_airborne != selector->required_airborne ||
	      bot->drop_recover)) ||
	    (!require_transition &&
	     (bot->drop_recover || (bot->drop_airborne && !bot->drop_walkoff))) ||
	    accept_drop.injection_applied != 0 ||
	    accept_drop.action_begins != 1 || accept_drop.action_begin_errors != 0 ||
	    accept_drop.historical_commands != expected_commands ||
	    accept_drop.commands != expected_commands ||
	    accept_drop.poses != expected_commands || accept_drop.arm_poses != 1 ||
	    accept_drop.final_historical_matches != expected_commands ||
	    accept_drop.final_historical_mismatches != 0 ||
	    accept_drop.historical_pending ||
	    accept_drop.injection_deferrals != expected_deferrals ||
	    accept_drop.injection_deferral_events != expected_deferrals ||
	    accept_drop.injection_deferral_order_errors != 0 ||
	    (expected_deferrals == 0 ?
	         accept_drop.injection_deferral_last_ordinal != 0 :
	         accept_drop.injection_deferral_last_ordinal !=
	             expected_commands - SG_DROP_LIVE_FRAME_STEPS) ||
	    accept_drop.boundary_enters != expected_deferrals ||
	    (SG_ACCEPT_DROP_LEGACY_A ?
	         (accept_drop.boundary_exits != 0 ||
	          accept_drop.boundary_results != 0) :
	         (accept_drop.boundary_exits != expected_deferrals ||
	          accept_drop.boundary_results != expected_deferrals)) ||
	    !AcceptPreInjectionContactLaw(selector, expected_deferrals))
		return false;
	if (SG_ACCEPT_DROP_LEGACY_A)
		return accept_drop.observer_began && accept_drop.observer_active &&
		       accept_drop.observer.progress.status == SG_REPLAY_RUNNING &&
		       accept_drop.observer.progress.reason == SG_REPLAY_REASON_NONE &&
		       accept_drop.observer.progress.elapsed_ms == expected_elapsed_ms &&
		       accept_drop.observer.progress.step_pending == expected_pending &&
		       (!accept_drop.observer.walkoff || bot->drop_walkoff) &&
		       (!accept_drop.observer.airborne || bot->drop_airborne) &&
		       (!require_transition ||
		        (accept_drop.observer.walkoff &&
		         accept_drop.observer.airborne == selector->required_airborne)) &&
		       !accept_drop.observer.recovery &&
		       accept_drop.observer_presteps == expected_commands &&
		       accept_drop.observer_poststeps == expected_poststeps &&
		       accept_drop.observer_boundaries == expected_deferrals &&
		       accept_drop.observer_command_matches == expected_commands &&
		       accept_drop.observer_command_mismatches == 0;
	return bot->drop_replay_active && bot->drop_replay_link == link_index &&
	       bot->drop_replay.progress.status == SG_REPLAY_RUNNING &&
	       bot->drop_replay.progress.reason == SG_REPLAY_REASON_NONE &&
	       bot->drop_replay.progress.elapsed_ms == expected_elapsed_ms &&
	       bot->drop_replay.progress.step_pending == expected_pending &&
	       (!bot->drop_replay.walkoff || bot->drop_walkoff) &&
	       (!bot->drop_replay.airborne || bot->drop_airborne) &&
	       (!require_transition ||
	        (bot->drop_replay.walkoff &&
	         bot->drop_replay.airborne == selector->required_airborne)) &&
	       !bot->drop_replay.recovery;
}

static qboolean AcceptInjectionPrecondition(const struct sg_bot_s *bot,
	int link_index, int step, const sg_accept_drop_selector_t *selector)
{
	return AcceptInjectionCadence(bot, link_index, step, selector, true);
}

static sg_accept_drop_injection_decision_t AcceptLateAirborneDecision(
	const struct sg_bot_s *bot, int link_index, int step,
	const sg_accept_drop_selector_t *selector)
{
	int selected_ms;
	qboolean authority_walkoff;
	qboolean authority_airborne;

	if (!AcceptLateAirborneSelector(selector) ||
	    !AcceptInjectionCadence(bot, link_index, step, selector, false))
		return SGAD_INJECTION_INVALID;
	selected_ms = (int)(accept_drop.commands * SG_REPLAY_STEP_MS);
	authority_walkoff = SG_ACCEPT_DROP_LEGACY_A ?
	    accept_drop.observer.walkoff : bot->drop_replay.walkoff;
	authority_airborne = SG_ACCEPT_DROP_LEGACY_A ?
	    accept_drop.observer.airborne : bot->drop_replay.airborne;
	if (bot->drop_walkoff && bot->drop_airborne && !bot->drop_recover &&
	    authority_walkoff && authority_airborne)
		return AcceptInjectionPrecondition(bot, link_index, step, selector) ?
		    SGAD_INJECTION_APPLY : SGAD_INJECTION_INVALID;
	if (bot->drop_recover || (bot->drop_airborne && !bot->drop_walkoff) ||
	    selected_ms >= selector->cost_ms)
		return SGAD_INJECTION_INVALID;
	return SGAD_INJECTION_DEFER;
}

static void AcceptRecordInjectionDeferral(const struct sg_bot_s *bot,
	int link_index, int step, const sg_accept_drop_selector_t *selector)
{
	unsigned int group = accept_drop.commands / SG_DROP_LIVE_FRAME_STEPS;
	unsigned int expected_ordinal =
	    (accept_drop.injection_deferrals + 1U) * SG_DROP_LIVE_FRAME_STEPS;
	int clock_elapsed = SG_ACCEPT_DROP_LEGACY_A ?
	    accept_drop.observer.progress.elapsed_ms :
	    bot->drop_replay.progress.elapsed_ms;
	qboolean clock_pending = SG_ACCEPT_DROP_LEGACY_A ?
	    accept_drop.observer.progress.step_pending :
	    bot->drop_replay.progress.step_pending;

	accept_drop.injection_deferrals++;
	accept_drop.injection_deferral_events++;
	if (accept_drop.commands != expected_ordinal ||
	    accept_drop.injection_deferral_events != accept_drop.injection_deferrals)
		accept_drop.injection_deferral_order_errors++;
	accept_drop.injection_deferral_last_ordinal = accept_drop.commands;
	AcceptLogPrefix("injection-deferred");
	sg_host.dprint(" link=%d step=%d group=%u command_ordinal=%u "
	               "deferral=%u boundary_count=%u selected_ms=%u "
	               "deadline_ms=%d walkoff=%d airborne=%d recovery=%d "
	               "clock_elapsed_ms=%d clock_pending=%d\n",
	    link_index, step, group, accept_drop.commands,
	    accept_drop.injection_deferrals, accept_drop.boundary_enters,
	    accept_drop.commands * SG_REPLAY_STEP_MS, selector->cost_ms,
	    bot->drop_walkoff, bot->drop_airborne, bot->drop_recover,
	    clock_elapsed, clock_pending);
}

static qboolean AcceptInjectFixture(struct sg_bot_s *bot, edict_t *ent,
	const sg_accept_drop_selector_t *selector)
{
	const rune_t *rune = SG_Rune();
	sg_drop_replay_state_t replay_before;
	sg_drop_replay_state_t observer_before;
	pmove_t pm;
	int health_before;
	int deadflag_before;
	int movetype_before;
	int commit_before;
	edict_t *previous_passent;
	int axis;

	if (!bot || !ent || !ent->client || !selector || !rune)
		return false;
	if (ent->client->ps.pmove.pm_type != PM_NORMAL ||
	    ent->client->ps.pmove.gravity != (short)rune->v3_header.gravity)
		return false;
	replay_before = bot->drop_replay;
	observer_before = accept_drop.observer;
	health_before = ent->health;
	deadflag_before = ent->deadflag;
	movetype_before = ent->movetype;
	commit_before = bot->commit_link;
	memset(&pm, 0, sizeof(pm));
	pm.s = ent->client->ps.pmove;
	for (axis = 0; axis < 3; axis++)
	{
		short fixed =
		    (short)(rune->seeds[selector->fixture_seed].origin[axis] * 8.0f);

		pm.s.origin[axis] = fixed;
		pm.s.velocity[axis] = 0;
		pm.cmd.angles[axis] = ANGLE2SHORT(ent->client->v_angle[axis]) -
		                      pm.s.delta_angles[axis];
	}
	pm.cmd.msec = 0;
	pm.snapinitial = memcmp(&pm.s, &ent->client->old_pmove,
	    sizeof(pm.s)) != 0;
	if (!pm.snapinitial)
		return false;
	pm.trace = AcceptFixtureTrace;
	pm.pointcontents = AcceptFixturePointContents;
	previous_passent = accept_fixture_passent;
	accept_fixture_passent = ent;
	sg_host.pmove(&pm);
	accept_fixture_passent = previous_passent;
	ent->client->ps.pmove = pm.s;
	ent->client->old_pmove = pm.s;
	for (axis = 0; axis < 3; axis++)
	{
		ent->s.origin[axis] = pm.s.origin[axis] * 0.125f;
		ent->s.old_origin[axis] = ent->s.origin[axis];
		ent->velocity[axis] = pm.s.velocity[axis] * 0.125f;
		ent->client->oldvelocity[axis] = 0.0f;
	}
	VectorCopy(pm.mins, ent->mins);
	VectorCopy(pm.maxs, ent->maxs);
	ent->viewheight = pm.viewheight;
	ent->waterlevel = pm.waterlevel;
	ent->watertype = pm.watertype;
	ent->groundentity = pm.groundentity;
	ent->groundentity_linkcount =
	    pm.groundentity ? pm.groundentity->linkcount : 0;
	sg_host.linkentity(ent);
	accept_drop.injection_zero_ms++;
	if (ent->health != health_before || ent->deadflag != deadflag_before ||
	    ent->movetype != movetype_before || ent->health <= 0 ||
	    ent->client->ps.pmove.pm_type != PM_NORMAL ||
	    ent->client->ps.pmove.gravity != (short)rune->v3_header.gravity ||
	    (ent->client->ps.pmove.pm_flags & PMF_TIME_TELEPORT) ||
	    bot->commit_link != commit_before ||
	    memcmp(&bot->drop_replay, &replay_before, sizeof(replay_before)) != 0 ||
	    memcmp(&accept_drop.observer, &observer_before,
	           sizeof(observer_before)) != 0 ||
	    accept_drop.injection_pointcontents == 0 ||
	    !AcceptFixtureSeedSnapshotValid(ent, selector))
		return false;
	accept_drop.injection_fixture_seed = selector->fixture_seed;
	for (axis = 0; axis < 3; axis++)
		accept_drop.injection_origin_bits[axis] =
		    AcceptFloatBits(ent->s.origin[axis]);
	accept_drop.injection_grounded = ent->groundentity != NULL;
	accept_drop.injection_support_valid = AcceptSupportValid(ent);
	accept_drop.injection_watertype = ent->watertype;
	accept_drop.injection_waterlevel = ent->waterlevel;
	accept_drop.injection_health = ent->health;
	accept_drop.injection_deadflag = ent->deadflag;
	accept_drop.injection_movetype = ent->movetype;
	accept_drop.injection_oldvelocity_zero = true;
	accept_drop.injection_terminal_geometry = selector->terminal_geometry;
	accept_drop.injection_recovery_geometry = selector->recovery_geometry;
	return true;
}

static void AcceptPrivateTerminate(struct sg_bot_s *bot, const char *where)
{
	qboolean already_retired;

	if (!bot)
		return;
	already_retired = bot->commit_link != accept_drop.link;
	accept_drop.private_stops++;
	AcceptLogPrefix("private-termination");
	sg_host.dprint(" link=%d where=%s checkpoint=%s already_retired=%d "
	               "selected_drop_commands=%u\n", accept_drop.link,
	    where ? where : "?", AcceptCheckpointToken(accept_drop.checkpoint),
	    already_retired, accept_drop.commands);
	if (!already_retired)
	{
		bot->commit_link = -1;
		bot->sticky_link = -1;
		bot->jump_link = -1;
		bot->jump_started = false;
		bot->drop_link = -1;
		bot->drop_started = false;
		bot->drop_walkoff = false;
		bot->drop_airborne = false;
		bot->drop_recover = false;
		bot->commit_until = 0.0f;
		SG_DropLiveReset(&bot->drop_replay, &bot->drop_replay_active,
		    &bot->drop_replay_link, &bot->drop_live_events);
	}
}

static void AcceptRejectInjected(struct sg_bot_s *bot, const char *diagnostic,
	const char *where)
{
	if (!AcceptEnabled() || accept_drop.finished)
		return;
	accept_drop.injection_errors++;
	AcceptPrivateTerminate(bot, where);
	accept_drop.finished = true;
	accept_drop.phase = SGAD_FAILED;
	accept_drop.final_outcome = SG_DROP_LIVE_FALLBACK;
	accept_drop.final_reason = SG_REPLAY_REASON_INVALID_STATE;
	accept_drop.finish_diagnostic = diagnostic;
	AcceptSummary("acceptance-rejected");
}

static void AcceptCompleteInjected(struct sg_bot_s *bot, const char *where)
{
	const sg_accept_drop_selector_t *selector;
	const char *failure;
	const char *terminal;

	if (!AcceptEnabled() || accept_drop.finished ||
	    accept_drop.requested_case < 1 ||
	    accept_drop.requested_case > SG_ACCEPT_DROP_CASE_COUNT)
		return;
	selector = &accept_selectors[accept_drop.requested_case - 1];
	AcceptPrivateTerminate(bot, where);
	failure = AcceptInjectedFinishFailure(&accept_drop, selector,
	    SG_ACCEPT_DROP_LEGACY_A);
	accept_drop.finished = true;
	if (failure)
	{
		accept_drop.phase = SGAD_FAILED;
		accept_drop.final_outcome = SG_DROP_LIVE_FALLBACK;
		accept_drop.final_reason = SG_REPLAY_REASON_INVALID_STATE;
		accept_drop.finish_diagnostic = failure;
		AcceptSummary("acceptance-rejected");
		return;
	}
	accept_drop.phase = SGAD_FINISHED;
	accept_drop.finish_diagnostic = "none";
	terminal = AcceptCheckpointToken(accept_drop.checkpoint);
	AcceptSummary(terminal);
}

static qboolean AcceptCommandEqual(const usercmd_t *first,
	const usercmd_t *second)
{
	int axis;

	if (!first || !second || first->msec != second->msec ||
	    first->buttons != second->buttons ||
	    first->forwardmove != second->forwardmove ||
	    first->sidemove != second->sidemove ||
	    first->upmove != second->upmove || first->impulse != second->impulse ||
	    first->lightlevel != second->lightlevel)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (first->angles[axis] != second->angles[axis])
			return false;
	return true;
}

static qboolean AcceptCommandZeroed(const usercmd_t *command)
{
	return command && command->msec == SG_REPLAY_STEP_MS &&
	       command->angles[PITCH] == 0 && command->angles[YAW] == 0 &&
	       command->angles[ROLL] == 0 && command->buttons == 0 &&
	       command->forwardmove == 0 && command->sidemove == 0 &&
	       command->upmove == 0 && command->impulse == 0 &&
	       command->lightlevel == 0;
}

static void AcceptObserverBegin(const struct sg_bot_s *bot, int link_index)
{
	const rune_t *rune = SG_Rune();
	const rune_link_t *link;
	sg_drop_replay_spec_t spec;
	sg_replay_observation_t observation;
	sg_replay_pose_t pose;
	sg_replay_status_t status;

	if (!SG_ACCEPT_DROP_LEGACY_A || !bot || !bot->ent || !rune ||
	    link_index < 0 || link_index >= rune->hdr.num_links)
		return;
	link = &rune->links[link_index];
	memset(&spec, 0, sizeof(spec));
	VectorCopy(rune->seeds[link->to].origin, spec.destination);
	VectorCopy(link->anchor, spec.lip);
	spec.heading = link->heading;
	spec.destination_water =
	    (rune->seeds[link->to].flags & RSF_WATER) != 0;
	spec.expected_arrival_ms = link->cost_ms;
	AcceptPoseFromEnt(bot->ent, &pose);
	observation = AcceptObservation(bot->ent);
	status = SG_DropReplayBegin(&accept_drop.observer, &spec, &pose,
	    &observation, bot->ent->client->oldvelocity[2]);
	accept_drop.observer_began = true;
	accept_drop.observer_active = status == SG_REPLAY_RUNNING;
	AcceptLogPrefix("observer-begin");
	sg_host.dprint(" link=%d status=%d reason=%s elapsed_ms=%d active=%d "
	               "old_frame_z_bits=%08x support_valid=%d\n",
	    link_index, (int)status,
	    AcceptReplayReasonToken(accept_drop.observer.progress.reason),
	    accept_drop.observer.progress.elapsed_ms,
	    accept_drop.observer_active,
	    AcceptFloatBits(bot->ent->client->oldvelocity[2]),
	    observation.ground_support_valid);
}

void SG_AcceptDropActionBegin(const struct sg_bot_s *bot, int link_index,
	const char *authority)
{
	if (!AcceptOwns(bot, link_index))
		return;
	accept_drop.action_begins++;
	if (accept_drop.action_begins != 1)
		accept_drop.action_begin_errors++;
	accept_drop.started = true;
	AcceptLogPrefix("action-begin");
	sg_host.dprint(" link=%d authority=%s action_begins=%u "
	               "action_begin_errors=%u\n", link_index,
	    authority ? authority : "?", accept_drop.action_begins,
	    accept_drop.action_begin_errors);
	AcceptObserverBegin(bot, link_index);
}

static void AcceptReducer(const struct sg_bot_s *bot)
{
	const sg_drop_replay_state_t *replay = &bot->drop_replay;

	sg_host.dprint(" replay_active=%d replay_link=%d replay_status=%d "
	               "replay_reason=%s elapsed_ms=%d step_pending=%d "
	               "reducer_walkoff=%d reducer_airborne=%d reducer_recovery=%d "
	               "observer_active=%d observer_status=%d observer_reason=%s "
	               "observer_elapsed_ms=%d observer_step_pending=%d "
	               "observer_walkoff=%d observer_airborne=%d "
	               "observer_recovery=%d",
	    bot->drop_replay_active, bot->drop_replay_link,
	    (int)replay->progress.status,
	    AcceptReplayReasonToken(replay->progress.reason),
	    replay->progress.elapsed_ms, replay->progress.step_pending,
	    replay->walkoff, replay->airborne, replay->recovery,
	    accept_drop.observer_active,
	    (int)accept_drop.observer.progress.status,
	    AcceptReplayReasonToken(accept_drop.observer.progress.reason),
	    accept_drop.observer.progress.elapsed_ms,
	    accept_drop.observer.progress.step_pending,
	    accept_drop.observer.walkoff, accept_drop.observer.airborne,
	    accept_drop.observer.recovery);
}

void SG_AcceptDropCommandHistorical(const struct sg_bot_s *bot,
	int link_index, int step, const usercmd_t *command)
{
	usercmd_t historical;
	usercmd_t observer_command;
	sg_replay_pose_t pose;
	sg_replay_status_t status = SG_REPLAY_FAILED;
	qboolean compared = false;
	qboolean equal = false;

	if (!AcceptOwns(bot, link_index) || !accept_drop.started || !command)
		return;
	historical = *command;
	if (accept_drop.historical_pending)
		accept_drop.final_historical_mismatches++;
	accept_drop.historical_command = historical;
	accept_drop.historical_step = step;
	accept_drop.historical_pending = true;
	accept_drop.historical_commands++;
	if (SG_ACCEPT_DROP_LEGACY_A && accept_drop.observer_active)
	{
		memset(&observer_command, 0, sizeof(observer_command));
		observer_command.msec = SG_REPLAY_STEP_MS;
		AcceptPoseFromEnt(bot->ent, &pose);
		status = SG_DropReplayPreStep(&accept_drop.observer, &pose,
		    &observer_command);
		accept_drop.observer_presteps++;
		compared = true;
		equal = status == SG_REPLAY_RUNNING &&
		    AcceptCommandEqual(&historical, &observer_command);
		if (equal)
			accept_drop.observer_command_matches++;
		else
			accept_drop.observer_command_mismatches++;
		if (status != SG_REPLAY_RUNNING)
			accept_drop.observer_active = false;
	}
	AcceptLogPrefix("command-historical");
	sg_host.dprint(" link=%d ordinal=%u step=%d msec=%u "
	               "angles_pitch=%d angles_yaw=%d angles_roll=%d "
	               "buttons=%u forwardmove=%d sidemove=%d upmove=%d "
	               "impulse=%u lightlevel=%u observer_compared=%d "
	               "observer_equal=%d observer_step_status=%d "
	               "observer_step_reason=%s",
	    link_index, accept_drop.historical_commands, step, historical.msec,
	    historical.angles[PITCH], historical.angles[YAW],
	    historical.angles[ROLL], historical.buttons,
	    historical.forwardmove, historical.sidemove, historical.upmove,
	    historical.impulse, historical.lightlevel, compared, equal,
	    (int)status, AcceptReplayReasonToken(
	        compared ? accept_drop.observer.progress.reason :
	                   SG_REPLAY_REASON_NONE));
	AcceptReducer(bot);
	sg_host.dprint("\n");
	if (compared)
	{
		AcceptLogPrefix("command-observer");
		sg_host.dprint(" link=%d ordinal=%u step=%d msec=%u "
		               "angles_pitch=%d angles_yaw=%d angles_roll=%d "
		               "buttons=%u forwardmove=%d sidemove=%d upmove=%d "
		               "impulse=%u lightlevel=%u logical_equal=%d\n",
		    link_index, accept_drop.observer_presteps, step,
		    observer_command.msec, observer_command.angles[PITCH],
		    observer_command.angles[YAW], observer_command.angles[ROLL],
		    observer_command.buttons, observer_command.forwardmove,
		    observer_command.sidemove, observer_command.upmove,
		    observer_command.impulse, observer_command.lightlevel, equal);
	}
}

void SG_AcceptDropCommand(const struct sg_bot_s *bot, int link_index,
	int step, const usercmd_t *command)
{
	qboolean historical_compared;
	qboolean historical_equal;

	if (!AcceptOwns(bot, link_index) || !accept_drop.started || !command)
		return;
	historical_compared = accept_drop.historical_pending &&
	    accept_drop.historical_step == step;
	historical_equal = historical_compared &&
	    AcceptCommandEqual(&accept_drop.historical_command, command);
	if (historical_equal)
		accept_drop.final_historical_matches++;
	else
		accept_drop.final_historical_mismatches++;
	accept_drop.historical_pending = false;
	accept_drop.commands++;
	if (AcceptCommandZeroed(command))
		accept_drop.zero_final_commands++;
	AcceptLogPrefix("command-final");
	sg_host.dprint(" link=%d ordinal=%u step=%d msec=%u "
	               "angles_pitch=%d angles_yaw=%d angles_roll=%d "
	               "buttons=%u forwardmove=%d sidemove=%d upmove=%d "
		               "impulse=%u lightlevel=%u zeroed=%d "
		               "historical_compared=%d historical_equal=%d",
	    link_index, accept_drop.commands, step, command->msec,
	    command->angles[PITCH], command->angles[YAW], command->angles[ROLL],
	    command->buttons, command->forwardmove, command->sidemove,
	    command->upmove, command->impulse, command->lightlevel,
	    AcceptCommandZeroed(command), historical_compared, historical_equal);
	AcceptReducer(bot);
	sg_host.dprint("\n");
}

void SG_AcceptDropPose(const struct sg_bot_s *bot, int link_index,
	int step, const edict_t *ent)
{
	qboolean timed_pose;
	unsigned int ordinal;

	if (!AcceptOwns(bot, link_index) || !ent)
		return;
	timed_pose = accept_drop.started;
	if (timed_pose)
		ordinal = ++accept_drop.poses;
	else
		ordinal = ++accept_drop.arm_poses;
	AcceptLogPrefix(timed_pose ? "pose-25ms" : "pose-arm-zero-ms");
	sg_host.dprint(" link=%d ordinal=%u step=%d "
	               "origin_bits=%08x/%08x/%08x "
	               "velocity_bits=%08x/%08x/%08x grounded=%d "
	               "ground_ent=%d watertype=%d waterlevel=%d "
	               "legacy_walkoff=%d legacy_airborne=%d legacy_recovery=%d",
	    link_index, ordinal, step,
	    AcceptFloatBits(ent->s.origin[0]), AcceptFloatBits(ent->s.origin[1]),
	    AcceptFloatBits(ent->s.origin[2]), AcceptFloatBits(ent->velocity[0]),
	    AcceptFloatBits(ent->velocity[1]), AcceptFloatBits(ent->velocity[2]),
	    ent->groundentity != NULL,
	    ent->groundentity ? (int)(ent->groundentity - g_edicts) : -1,
	    ent->watertype, ent->waterlevel, bot->drop_walkoff,
	    bot->drop_airborne, bot->drop_recover);
	AcceptReducer(bot);
	sg_host.dprint("\n");
}

qboolean SG_AcceptDropAfterStep(struct sg_bot_s *bot, int link_index,
	int step, edict_t *ent)
{
	const sg_accept_drop_selector_t *selector;
	sg_replay_status_t observer_status = SG_REPLAY_FAILED;
	qboolean observer_sampled = false;

	if (!AcceptOwns(bot, link_index) || !ent)
		return false;
	selector = &accept_selectors[accept_drop.requested_case - 1];
	if (SG_ACCEPT_DROP_LEGACY_A && accept_drop.observer_active && step < 3)
	{
		sg_replay_observation_t observation = AcceptObservation(ent);
		sg_replay_pose_t pose;
		qboolean events_applied;

		events_applied = AcceptObserverEventsApply(step, &observation);
		AcceptPoseFromEnt(ent, &pose);
		observer_status = SG_DropReplayPostStep(&accept_drop.observer, &pose,
		    &observation);
		accept_drop.observer_poststeps++;
		observer_sampled = true;
		if (observer_status != SG_REPLAY_RUNNING)
			accept_drop.observer_active = false;
		AcceptLogPrefix("observer-poststep");
		sg_host.dprint(" link=%d step=%d status=%d reason=%s elapsed_ms=%d "
		               "support_valid=%d events_applied=%d contaminated=%d "
		               "door_passed=%d active=%d\n", link_index, step,
		    (int)observer_status,
		    AcceptReplayReasonToken(accept_drop.observer.progress.reason),
		    accept_drop.observer.progress.elapsed_ms,
		    observation.ground_support_valid, events_applied,
		    observation.contaminated, observation.door_passed,
		    accept_drop.observer_active);
	}
	if (selector->fixture_kind != SGAD_FIXTURE_NONE &&
	    !accept_drop.injection_applied && step == selector->injection_step)
	{
		sg_accept_drop_injection_decision_t decision =
		    AcceptLateAirborneSelector(selector) ?
		        AcceptLateAirborneDecision(bot, link_index, step, selector) :
		        SGAD_INJECTION_APPLY;

		if (decision == SGAD_INJECTION_DEFER)
		{
			AcceptRecordInjectionDeferral(bot, link_index, step, selector);
		}
		else if (decision == SGAD_INJECTION_INVALID)
		{
			AcceptRejectInjected(bot, "finish-injection-precondition",
			    "after-selected-command-precondition");
			return true;
		}
		if (decision == SGAD_INJECTION_DEFER)
			goto after_injection;
		accept_drop.injection_attempts++;
		accept_drop.injection_step = step;
		accept_drop.injection_frame = level.framenum;
		accept_drop.injection_pre_walkoff = bot->drop_walkoff;
		accept_drop.injection_pre_airborne = bot->drop_airborne;
		accept_drop.injection_pre_recovery = bot->drop_recover;
		if (!AcceptInjectionPrecondition(bot, link_index, step, selector))
		{
			AcceptRejectInjected(bot, "finish-injection-precondition",
			    "after-selected-command-precondition");
			return true;
		}
		if (accept_drop.injection_pre_contact_captured)
		{
			AcceptRejectInjected(bot, "finish-injection-precondition",
			    "after-selected-command-contact-baseline");
			return true;
		}
		accept_drop.injection_pre_contact_captured = true;
		accept_drop.injection_pre_arrival_samples =
		    accept_drop.result_arrival_samples;
		if (!AcceptInjectFixture(bot, ent, selector))
		{
			AcceptRejectInjected(bot, "finish-injection-fixture",
			    "after-selected-command-fixture");
			return true;
		}
		accept_drop.injection_applied++;
		accept_drop.injection_order_stage = SGAD_ORDER_INJECTED;
		AcceptLogPrefix("injection-applied");
		sg_host.dprint(" link=%d fixture_seed=%d step=%d command_ordinal=%u "
		               "origin_bits=%08x/%08x/%08x grounded=%d ground_ent=%d "
		               "support_valid=%d watertype=%d waterlevel=%d "
		               "pre_walkoff=%d pre_airborne=%d pre_recovery=%d "
		               "pre_contact_captured=%d pre_arrival_samples=%u "
		               "terminal_geometry=%d recovery_geometry=%d "
		               "fixture_boundary_mode=%s "
		               "zero_ms=%u fixture_pmove_traces=%u "
		               "fixture_pointcontents=%u health=%d deadflag=%d "
		               "movetype=%d oldvelocity_zero=%d\n",
		    link_index, selector->fixture_seed, step, accept_drop.commands,
		    accept_drop.injection_origin_bits[0],
		    accept_drop.injection_origin_bits[1],
		    accept_drop.injection_origin_bits[2],
		    accept_drop.injection_grounded,
		    ent->groundentity ? (int)(ent->groundentity - g_edicts) : -1,
		    accept_drop.injection_support_valid,
		    accept_drop.injection_watertype,
		    accept_drop.injection_waterlevel,
		    accept_drop.injection_pre_walkoff,
		    accept_drop.injection_pre_airborne,
		    accept_drop.injection_pre_recovery,
		    accept_drop.injection_pre_contact_captured,
		    accept_drop.injection_pre_arrival_samples,
		    accept_drop.injection_terminal_geometry,
		    accept_drop.injection_recovery_geometry,
		    AcceptFixtureBoundaryToken(selector->fixture_boundary),
		    accept_drop.injection_zero_ms,
		    accept_drop.injection_pmove_traces,
		    accept_drop.injection_pointcontents, accept_drop.injection_health,
		    accept_drop.injection_deadflag, accept_drop.injection_movetype,
		    accept_drop.injection_oldvelocity_zero);
	}
after_injection:
	if (selector->fixture_boundary == SGAD_FIXTURE_BOUNDARY_POST_COMMAND &&
	    accept_drop.injection_applied == 1 &&
	    step == selector->injection_step + 1)
	{
		if (!AcceptCapturePostCommandFixture(bot, ent, selector))
		{
			AcceptRejectInjected(bot, "finish-injection-order",
			    "after-real-command-post-command-capture");
			return true;
		}
		AcceptLogPrefix("fixture-post-command-pose");
		sg_host.dprint(" link=%d step=%d mode=%s captures=%u "
		               "origin_bits=%08x/%08x/%08x velocity_bits=%08x/%08x/%08x "
		               "old_origin_bits=%08x/%08x/%08x "
		               "oldvelocity_bits=%08x/%08x/%08x "
		               "pmove_origin=%d/%d/%d pmove_velocity=%d/%d/%d "
		               "pmove_type=%d pmove_gravity=%d pmove_flags=%d",
		    link_index, step, AcceptFixtureBoundaryToken(selector->fixture_boundary),
		    accept_drop.injection_post_command_captures,
		    accept_drop.post_command.origin_bits[0],
		    accept_drop.post_command.origin_bits[1],
		    accept_drop.post_command.origin_bits[2],
		    accept_drop.post_command.velocity_bits[0],
		    accept_drop.post_command.velocity_bits[1],
		    accept_drop.post_command.velocity_bits[2],
		    accept_drop.post_command.old_origin_bits[0],
		    accept_drop.post_command.old_origin_bits[1],
		    accept_drop.post_command.old_origin_bits[2],
		    accept_drop.post_command.oldvelocity_bits[0],
		    accept_drop.post_command.oldvelocity_bits[1],
		    accept_drop.post_command.oldvelocity_bits[2],
		    accept_drop.post_command.pmove_origin[0],
		    accept_drop.post_command.pmove_origin[1],
		    accept_drop.post_command.pmove_origin[2],
		    accept_drop.post_command.pmove_velocity[0],
		    accept_drop.post_command.pmove_velocity[1],
		    accept_drop.post_command.pmove_velocity[2],
		    accept_drop.post_command.pmove_type,
		    accept_drop.post_command.pmove_gravity,
		    accept_drop.post_command.pmove_flags);
		sg_host.dprint("\n");
		AcceptLogPrefix("fixture-post-command-state");
		sg_host.dprint(" link=%d step=%d mode=%s captures=%u health=%d "
		               "deadflag=%d movetype=%d grounded=%d ground_ent=%d "
		               "ground_linkcount=%d support_valid=%d watertype=%d "
		               "waterlevel=%d terminal_geometry=%d recovery_geometry=%d "
		               "historical_commands=%u final_commands=%u poses=%u "
		               "final_matches=%u final_mismatches=%u historical_pending=%d",
		    link_index, step, AcceptFixtureBoundaryToken(selector->fixture_boundary),
		    accept_drop.injection_post_command_captures,
		    accept_drop.post_command.health,
		    accept_drop.post_command.deadflag,
		    accept_drop.post_command.movetype,
		    accept_drop.post_command.grounded,
		    accept_drop.post_command.groundentity ?
		        (int)(accept_drop.post_command.groundentity - g_edicts) : -1,
		    accept_drop.post_command.groundentity_linkcount,
		    accept_drop.post_command.support_valid,
		    accept_drop.post_command.watertype,
		    accept_drop.post_command.waterlevel,
		    accept_drop.post_command.terminal_geometry,
		    accept_drop.post_command.recovery_geometry,
		    accept_drop.post_command.historical_commands,
		    accept_drop.post_command.commands,
		    accept_drop.post_command.poses,
		    accept_drop.post_command.final_historical_matches,
		    accept_drop.post_command.final_historical_mismatches,
		    accept_drop.post_command.historical_pending);
		sg_host.dprint("\n");
		AcceptLogPrefix("fixture-post-command-reducer");
		sg_host.dprint(" link=%d step=%d captures=%u r_active=%d r_link=%d "
		               "r_status=%d r_reason=%s r_elapsed=%d r_arrival=%d "
		               "r_pending=%d r_walkoff=%d r_airborne=%d r_recovery=%d "
		               "o_active=%d o_status=%d o_reason=%s o_elapsed=%d "
		               "o_arrival=%d o_pending=%d o_walkoff=%d o_airborne=%d "
		               "o_recovery=%d o_presteps=%u o_poststeps=%u "
		               "o_boundaries=%u o_matches=%u o_mismatches=%u",
		    link_index, step, accept_drop.injection_post_command_captures,
		    accept_drop.post_command.reducer_active,
		    accept_drop.post_command.reducer_link,
		    (int)accept_drop.post_command.reducer_status,
		    AcceptReplayReasonToken(accept_drop.post_command.reducer_reason),
		    accept_drop.post_command.reducer_elapsed_ms,
		    accept_drop.post_command.reducer_arrival_ms,
		    accept_drop.post_command.reducer_step_pending,
		    accept_drop.post_command.reducer_walkoff,
		    accept_drop.post_command.reducer_airborne,
		    accept_drop.post_command.reducer_recovery,
		    accept_drop.post_command.observer_active,
		    (int)accept_drop.post_command.observer_status,
		    AcceptReplayReasonToken(accept_drop.post_command.observer_reason),
		    accept_drop.post_command.observer_elapsed_ms,
		    accept_drop.post_command.observer_arrival_ms,
		    accept_drop.post_command.observer_step_pending,
		    accept_drop.post_command.observer_walkoff,
		    accept_drop.post_command.observer_airborne,
		    accept_drop.post_command.observer_recovery,
		    accept_drop.post_command.observer_presteps,
		    accept_drop.post_command.observer_poststeps,
		    accept_drop.post_command.observer_boundaries,
		    accept_drop.post_command.observer_command_matches,
		    accept_drop.post_command.observer_command_mismatches);
		sg_host.dprint("\n");
	}
	AcceptLogPrefix("after-step");
	sg_host.dprint(" link=%d step=%d grounded=%d waterlevel=%d "
	               "legacy_walkoff=%d legacy_airborne=%d legacy_recovery=%d "
	               "observer_poststep_sampled=%d observer_poststep_status=%d",
	    link_index, step, ent->groundentity != NULL, ent->waterlevel,
	    bot->drop_walkoff, bot->drop_airborne, bot->drop_recover,
	    observer_sampled, (int)observer_status);
	AcceptReducer(bot);
	sg_host.dprint("\n");
	if (selector->fixture_kind != SGAD_FIXTURE_NONE &&
	    !SG_ACCEPT_DROP_LEGACY_A &&
	    selector->rev2_checkpoint == SGAD_CHECKPOINT_REV2_RUNNING &&
	    accept_drop.injection_applied == 1 &&
	    accept_drop.boundary_results == 1 && step == 0 &&
	    accept_drop.commands == 5 && accept_drop.poses == 5)
	{
		accept_drop.production_status = bot->drop_replay.progress.status;
		accept_drop.production_reason = bot->drop_replay.progress.reason;
		accept_drop.production_elapsed_ms =
		    bot->drop_replay.progress.elapsed_ms;
		accept_drop.production_arrival_ms =
		    bot->drop_replay.progress.arrival_ms;
		accept_drop.checkpoint = SGAD_CHECKPOINT_REV2_RUNNING;
		accept_drop.checkpoint_frame = level.framenum;
		if (accept_drop.injection_order_stage != SGAD_ORDER_SG_FRAME)
			accept_drop.injection_order_errors++;
		accept_drop.injection_order_stage = SGAD_ORDER_CHECKPOINT;
		AcceptLogPrefix("private-stop-requested");
		sg_host.dprint(" link=%d checkpoint=%s selected_drop_commands=%u "
		               "where=post-command5-poststep\n", link_index,
		    AcceptCheckpointToken(accept_drop.checkpoint),
		    accept_drop.commands);
		return true;
	}
	return false;
}

qboolean SG_AcceptDropStopBeforeEmit(const struct sg_bot_s *bot,
	int link_index)
{
	if (!AcceptOwns(bot, link_index) || !accept_drop.stop_before_emit)
		return false;
	accept_drop.stop_before_emit = false;
	AcceptLogPrefix("private-stop-requested");
	sg_host.dprint(" link=%d checkpoint=%s selected_drop_commands=%u "
	               "where=before-emit\n",
	    link_index, AcceptCheckpointToken(accept_drop.checkpoint),
	    accept_drop.commands);
	return true;
}

void SG_AcceptDropGenericHandoffBegin(const struct sg_bot_s *bot, int bestlink,
	const usercmd_t *command, int substeps)
{
	qboolean expected;

	if (!AcceptEnabled() || !accept_drop.armed || bot != accept_drop.bot ||
	    !accept_drop.injection_applied || !command)
		return;
	expected = accept_drop.checkpoint ==
	               SGAD_CHECKPOINT_LEGACY_SHORT_CONTACT ||
	           accept_drop.checkpoint == SGAD_CHECKPOINT_REV2_SHORT_LANDING;
	if (!expected)
		return;
	accept_drop.generic_handoff_begins++;
	if (accept_drop.generic_handoff_pending)
	{
		accept_drop.injection_errors++;
		return;
	}
	accept_drop.generic_handoff_pending = true;
	accept_drop.generic_handoff_frame = level.framenum;
	accept_drop.generic_handoff_bestlink = bestlink;
	accept_drop.generic_handoff_substeps = substeps;
	accept_drop.generic_handoff_total_msec = command->msec;
	accept_drop.generic_handoff_command = *command;
	accept_drop.generic_handoff_begin_valid =
	    accept_drop.generic_handoff_begins == 1 &&
	    level.framenum == accept_drop.checkpoint_frame && bestlink == -1 &&
	    bot->commit_link != accept_drop.link &&
	    command->msec == SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC &&
	    substeps == SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS;
	if (!accept_drop.generic_handoff_begin_valid)
		accept_drop.injection_errors++;
}

void SG_AcceptDropGenericHandoffEnd(const struct sg_bot_s *bot, int bestlink,
	int substeps, int total_msec)
{
	const usercmd_t *command;
	qboolean expected;
	qboolean valid;

	if (!AcceptEnabled() || !accept_drop.armed || bot != accept_drop.bot ||
	    !accept_drop.injection_applied)
		return;
	expected = accept_drop.checkpoint ==
	               SGAD_CHECKPOINT_LEGACY_SHORT_CONTACT ||
	           accept_drop.checkpoint == SGAD_CHECKPOINT_REV2_SHORT_LANDING;
	if (!expected)
		return;
	accept_drop.generic_handoff_ends++;
	if (!accept_drop.generic_handoff_pending)
	{
		accept_drop.injection_errors++;
		return;
	}
	accept_drop.generic_handoff_completed_substeps += (unsigned int)substeps;
	valid = accept_drop.generic_handoff_begin_valid &&
	        accept_drop.generic_handoff_begins == 1 &&
	        accept_drop.generic_handoff_ends == 1 &&
	        accept_drop.generic_handoff_frame == level.framenum &&
	        accept_drop.generic_handoff_bestlink == bestlink &&
	        accept_drop.generic_handoff_substeps == substeps &&
	        accept_drop.generic_handoff_total_msec == total_msec &&
	        substeps == SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS &&
	        total_msec == SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC;
	accept_drop.generic_handoff_pending = false;
	accept_drop.generic_handoffs++;
	if (!valid)
		accept_drop.injection_errors++;
	command = &accept_drop.generic_handoff_command;
	AcceptLogPrefix("generic-handoff");
	sg_host.dprint(" link=%d bestlink=%d ordinal=%u think_over=0 substeps=%d "
	               "msec=%u angles_pitch=%d angles_yaw=%d angles_roll=%d "
	               "buttons=%u forwardmove=%d sidemove=%d upmove=%d "
	               "impulse=%u lightlevel=%u valid=%d "
	               "selected_drop_commands=%u consumed_substeps=%u\n",
	    accept_drop.link, bestlink, accept_drop.generic_handoffs, substeps,
	    command->msec, command->angles[PITCH], command->angles[YAW],
	    command->angles[ROLL], command->buttons, command->forwardmove,
	    command->sidemove, command->upmove, command->impulse,
	    command->lightlevel, valid, accept_drop.commands,
	    accept_drop.generic_handoff_completed_substeps);
}

static qboolean AcceptCountDelta(unsigned int value, unsigned int baseline,
	unsigned int expected)
{
	return value >= baseline && value - baseline == expected;
}

/* Record every real pre-injection boundary independently.  Late airborne
 * admission permits one very specific sampled boundary immediately before
 * injection in revision 2; a cumulative nonzero counter is not sufficient
 * evidence because it would lose which boundary produced the sample. */
static void AcceptPreContactBoundaryBegin(
	const sg_accept_drop_selector_t *selector)
{
	if (!AcceptLateAirborneSelector(selector) ||
	    accept_drop.injection_applied != 0)
		return;
	if (accept_drop.pre_contact_boundary_open)
		accept_drop.pre_contact_errors++;
	accept_drop.pre_contact_boundary_open = true;
	accept_drop.pre_contact_boundary_ordinal = accept_drop.boundary_enters;
	accept_drop.pre_contact_arrival_callbacks = accept_drop.arrival_callbacks;
	accept_drop.pre_contact_recovery_callbacks = accept_drop.recovery_callbacks;
	accept_drop.pre_contact_arrival_predicates = accept_drop.arrival_predicates;
	accept_drop.pre_contact_recovery_predicates = accept_drop.recovery_predicates;
	accept_drop.pre_contact_arrival_predicate_results =
	    accept_drop.arrival_predicate_results;
	accept_drop.pre_contact_recovery_predicate_results =
	    accept_drop.recovery_predicate_results;
	accept_drop.pre_contact_arrival_predicate_true =
	    accept_drop.arrival_predicate_true;
	accept_drop.pre_contact_recovery_predicate_true =
	    accept_drop.recovery_predicate_true;
	accept_drop.pre_contact_arrival_traces = accept_drop.arrival_traces;
	accept_drop.pre_contact_recovery_traces = accept_drop.recovery_traces;
	accept_drop.pre_contact_arrival_trace_true = accept_drop.arrival_trace_true;
	accept_drop.pre_contact_recovery_trace_true = accept_drop.recovery_trace_true;
	accept_drop.pre_contact_observer_arrival_cached =
	    accept_drop.observer_arrival_cached;
	accept_drop.pre_contact_observer_arrival_inferred =
	    accept_drop.observer_arrival_inferred;
	accept_drop.pre_contact_observer_arrival_cached_true =
	    accept_drop.observer_arrival_cached_true;
	accept_drop.pre_contact_observer_recovery_cached =
	    accept_drop.observer_recovery_cached;
	accept_drop.pre_contact_observer_recovery_inferred =
	    accept_drop.observer_recovery_inferred;
	accept_drop.pre_contact_observer_recovery_cached_true =
	    accept_drop.observer_recovery_cached_true;
}

static qboolean AcceptPreContactCommonDelta(unsigned int arrival_samples)
{
	return AcceptCountDelta(accept_drop.arrival_callbacks,
	           accept_drop.pre_contact_arrival_callbacks, arrival_samples) &&
	       AcceptCountDelta(accept_drop.recovery_callbacks,
	           accept_drop.pre_contact_recovery_callbacks, 0) &&
	       AcceptCountDelta(accept_drop.arrival_predicates,
	           accept_drop.pre_contact_arrival_predicates, arrival_samples) &&
	       AcceptCountDelta(accept_drop.recovery_predicates,
	           accept_drop.pre_contact_recovery_predicates, 0) &&
	       AcceptCountDelta(accept_drop.arrival_predicate_results,
	           accept_drop.pre_contact_arrival_predicate_results,
	           arrival_samples) &&
	       AcceptCountDelta(accept_drop.recovery_predicate_results,
	           accept_drop.pre_contact_recovery_predicate_results, 0) &&
	       AcceptCountDelta(accept_drop.arrival_predicate_true,
	           accept_drop.pre_contact_arrival_predicate_true, 0) &&
	       AcceptCountDelta(accept_drop.recovery_predicate_true,
	           accept_drop.pre_contact_recovery_predicate_true, 0) &&
	       AcceptCountDelta(accept_drop.arrival_traces,
	           accept_drop.pre_contact_arrival_traces, 0) &&
	       AcceptCountDelta(accept_drop.recovery_traces,
	           accept_drop.pre_contact_recovery_traces, 0) &&
	       AcceptCountDelta(accept_drop.arrival_trace_true,
	           accept_drop.pre_contact_arrival_trace_true, 0) &&
	       AcceptCountDelta(accept_drop.recovery_trace_true,
	           accept_drop.pre_contact_recovery_trace_true, 0);
}

static void AcceptPreContactBoundaryCloseLegacy(qboolean pending,
	sg_replay_status_t status)
{
	qboolean valid;

	if (!AcceptLateAirborneSelector(
	        &accept_selectors[accept_drop.requested_case - 1]) ||
	    accept_drop.injection_applied != 0)
		return;
	valid = accept_drop.pre_contact_boundary_open && pending &&
	    accept_drop.pre_contact_boundary_ordinal == accept_drop.boundary_enters &&
	    AcceptPreContactCommonDelta(0) &&
	    AcceptCountDelta(accept_drop.observer_arrival_cached,
	        accept_drop.pre_contact_observer_arrival_cached, 0) &&
	    AcceptCountDelta(accept_drop.observer_arrival_inferred,
	        accept_drop.pre_contact_observer_arrival_inferred, 1) &&
	    AcceptCountDelta(accept_drop.observer_arrival_cached_true,
	        accept_drop.pre_contact_observer_arrival_cached_true, 0) &&
	    AcceptCountDelta(accept_drop.observer_recovery_cached,
	        accept_drop.pre_contact_observer_recovery_cached, 0) &&
	    AcceptCountDelta(accept_drop.observer_recovery_inferred,
	        accept_drop.pre_contact_observer_recovery_inferred, 1) &&
	    AcceptCountDelta(accept_drop.observer_recovery_cached_true,
	        accept_drop.pre_contact_observer_recovery_cached_true, 0) &&
	    status == SG_REPLAY_RUNNING && accept_drop.observer_active &&
	    accept_drop.observer.progress.reason == SG_REPLAY_REASON_NONE &&
	    !accept_drop.observer.recovery;
	accept_drop.pre_contact_validated++;
	if (!valid)
		accept_drop.pre_contact_errors++;
	accept_drop.pre_contact_boundary_open = false;
}

static void AcceptPreContactBoundaryCloseRev2(const struct sg_bot_s *bot,
	int link_index, const sg_drop_live_result_t *result)
{
	unsigned int sampled;
	qboolean valid;

	if (!AcceptLateAirborneSelector(
	        &accept_selectors[accept_drop.requested_case - 1]) ||
	    accept_drop.injection_applied != 0)
		return;
	sampled = result && result->arrival_sampled ? 1U : 0U;
	valid = accept_drop.pre_contact_boundary_open && result &&
	    accept_drop.pre_contact_boundary_ordinal == accept_drop.boundary_enters &&
	    accept_drop.boundary_exits == accept_drop.pre_contact_boundary_ordinal &&
	    AcceptPreContactCommonDelta(sampled) &&
	    !result->arrived && !result->recovery_sampled &&
	    !result->recovery_ready && !result->recovery_started &&
	    result->outcome == SG_DROP_LIVE_RUNNING &&
	    result->failure == SG_DROP_LIVE_FAILURE_NONE &&
	    result->replay_reason == SG_REPLAY_REASON_NONE && bot &&
	    bot->drop_replay_active && bot->drop_replay_link == link_index &&
	    bot->drop_replay.progress.status == SG_REPLAY_RUNNING &&
	    bot->drop_replay.progress.reason == SG_REPLAY_REASON_NONE &&
	    !bot->drop_replay.recovery;
	accept_drop.pre_contact_validated++;
	if (sampled)
	{
		accept_drop.pre_contact_sampled++;
		accept_drop.pre_contact_last_sampled =
		    accept_drop.pre_contact_boundary_ordinal;
	}
	if (!valid)
		accept_drop.pre_contact_errors++;
	accept_drop.pre_contact_boundary_open = false;
}

void SG_AcceptDropBoundary(const struct sg_bot_s *bot, int link_index,
	const char *edge, const sg_drop_live_result_t *result,
	const sg_drop_live_events_t *events)
{
	const sg_accept_drop_selector_t *selector;

	if (!AcceptOwns(bot, link_index))
		return;
	selector = &accept_selectors[accept_drop.requested_case - 1];
	if (edge && strncmp(edge, "boundary-enter-", 15) == 0)
	{
		accept_drop.boundary_enters++;
		AcceptPreContactBoundaryBegin(selector);
		if (selector->fixture_kind != SGAD_FIXTURE_NONE &&
		    accept_drop.injection_applied == 1)
		{
			qboolean fixture_valid =
			    AcceptFixtureSnapshotValid(bot->ent, selector);
			uint64_t fixture_mismatch = fixture_valid ? 0 :
			    AcceptFixtureSnapshotMismatch(bot->ent, selector);

			accept_drop.injection_snapshot_mismatch_mask = fixture_mismatch;
			accept_drop.injection_boundary_checks++;
			if (selector->fixture_boundary ==
			    SGAD_FIXTURE_BOUNDARY_POST_COMMAND)
				accept_drop.injection_post_command_validations++;
			if (accept_drop.injection_order_stage != SGAD_ORDER_SG_FRAME ||
			    level.framenum != accept_drop.injection_frame + 1 ||
			    !fixture_valid)
				accept_drop.injection_order_errors++;
			AcceptLogPrefix("fixture-boundary-validation");
			sg_host.dprint(" link=%d edge=%s mode=%s valid=%d checks=%u "
			               "post_command_captures=%u "
			               "post_command_validations=%u order_stage=%d "
			               "order_errors=%u snapshot_mismatch=%016llx "
			               "snapshot_first=%s",
			    link_index, edge, AcceptFixtureBoundaryToken(
			        selector->fixture_boundary), fixture_valid,
			    accept_drop.injection_boundary_checks,
			    accept_drop.injection_post_command_captures,
			    accept_drop.injection_post_command_validations,
			    accept_drop.injection_order_stage,
			    accept_drop.injection_order_errors,
			    (unsigned long long)fixture_mismatch,
			    AcceptSnapshotMismatchFirst(fixture_mismatch));
			AcceptReducer(bot);
			sg_host.dprint("\n");
		}
	}
	if (edge && strncmp(edge, "boundary-exit-", 14) == 0)
	{
		accept_drop.boundary_exits++;
		if (!SG_ACCEPT_DROP_LEGACY_A)
			AcceptPreContactBoundaryCloseRev2(bot, link_index, result);
	}
	if (SG_ACCEPT_DROP_LEGACY_A && edge &&
	    strcmp(edge, "boundary-enter-legacy") == 0)
	{
		accept_drop.boundary_capture_open = true;
		accept_drop.boundary_arrival_sampled = false;
		accept_drop.boundary_arrival_result = false;
		accept_drop.boundary_recovery_sampled = false;
		accept_drop.boundary_recovery_result = false;
	}
	AcceptLogPrefix(edge ? edge : "boundary");
	sg_host.dprint(" link=%d", link_index);
	AcceptReducer(bot);
	if (result)
	{
		accept_drop.boundary_results++;
		if (result->arrival_sampled)
			accept_drop.result_arrival_samples++;
		if (result->arrived)
			accept_drop.result_arrivals++;
		if (result->recovery_sampled)
			accept_drop.result_recovery_samples++;
		if (result->recovery_ready)
			accept_drop.result_recovery_ready++;
		if (result->recovery_started)
		{
			accept_drop.result_recovery_started++;
			if (accept_drop.production_recovery_start_ms == 0)
				accept_drop.production_recovery_start_ms =
				    bot->drop_replay.progress.elapsed_ms;
		}
		accept_drop.production_status = bot->drop_replay.progress.status;
		accept_drop.production_reason = bot->drop_replay.progress.reason;
		accept_drop.production_elapsed_ms =
		    bot->drop_replay.progress.elapsed_ms;
		accept_drop.production_arrival_ms =
		    bot->drop_replay.progress.arrival_ms;
		sg_host.dprint(" outcome=%d failure=%d result_reason=%s "
		               "arrival_sampled=%d arrived=%d recovery_sampled=%d "
		               "recovery_ready=%d recovery_started=%d contaminated=%d "
		               "door_passed=%d pre_contact_validated=%u "
		               "pre_contact_sampled=%u pre_contact_last_sampled=%u "
		               "pre_contact_errors=%u",
		    (int)result->outcome, (int)result->failure,
		    AcceptReplayReasonToken(result->replay_reason),
		    result->arrival_sampled, result->arrived,
		    result->recovery_sampled, result->recovery_ready,
		    result->recovery_started, events ? events->contaminated : 0,
		    events ? events->door_passed : 0,
		    accept_drop.pre_contact_validated,
		    accept_drop.pre_contact_sampled,
		    accept_drop.pre_contact_last_sampled,
		    accept_drop.pre_contact_errors);
		accept_drop.final_outcome = result->outcome;
		accept_drop.final_reason = result->replay_reason;
		if (result->arrived)
			accept_drop.last_arrival = true;
		if (result->recovery_ready)
			accept_drop.last_recovery = true;
		if (selector->fixture_kind != SGAD_FIXTURE_NONE &&
		    !SG_ACCEPT_DROP_LEGACY_A &&
		    accept_drop.injection_applied == 1 &&
		    selector->rev2_checkpoint ==
		        SGAD_CHECKPOINT_REV2_SHORT_LANDING &&
		    result->outcome == SG_DROP_LIVE_FAILED &&
		    result->replay_reason == SG_REPLAY_REASON_SHORT_LANDING)
		{
			accept_drop.checkpoint = SGAD_CHECKPOINT_REV2_SHORT_LANDING;
			accept_drop.checkpoint_frame = level.framenum;
			accept_drop.injection_order_stage = SGAD_ORDER_CHECKPOINT;
		}
	}
	sg_host.dprint("\n");
}

static qboolean AcceptContactOwns(const edict_t *ent,
	const rune_link_t *link)
{
	rune_t *rune = SG_Rune();

	return AcceptEnabled() && accept_drop.armed && accept_drop.bot &&
	       ent == accept_drop.bot->ent && rune && link &&
	       link == &rune->links[accept_drop.link];
}

void SG_AcceptDropCallback(const char *kind, const edict_t *ent,
	const rune_link_t *link)
{
	if (!AcceptContactOwns(ent, link))
		return;
	if (kind && strcmp(kind, "recovery") == 0)
		accept_drop.recovery_callbacks++;
	else
		accept_drop.arrival_callbacks++;
	AcceptLogPrefix("callback-entry");
	sg_host.dprint(" link=%d kind=%s arrival_callback=%u "
	               "recovery_callback=%u\n", accept_drop.link,
	    kind ? kind : "?", accept_drop.arrival_callbacks,
	    accept_drop.recovery_callbacks);
}

void SG_AcceptDropPredicate(const char *kind, const edict_t *ent,
	const rune_link_t *link)
{
	if (!AcceptContactOwns(ent, link))
		return;
	if (kind && strcmp(kind, "recovery") == 0)
		accept_drop.recovery_predicates++;
	else
		accept_drop.arrival_predicates++;
	AcceptLogPrefix("predicate-entry");
	sg_host.dprint(" link=%d kind=%s arrival_predicate=%u "
	               "recovery_predicate=%u\n", accept_drop.link,
	    kind ? kind : "?", accept_drop.arrival_predicates,
	    accept_drop.recovery_predicates);
}

void SG_AcceptDropPredicateResult(const char *kind, const edict_t *ent,
	const rune_link_t *link, qboolean accepted)
{
	qboolean recovery;

	if (!AcceptContactOwns(ent, link))
		return;
	recovery = kind && strcmp(kind, "recovery") == 0;
	if (recovery)
	{
		accept_drop.recovery_predicate_results++;
		if (accepted)
			accept_drop.recovery_predicate_true++;
	}
	else
	{
		accept_drop.arrival_predicate_results++;
		if (accepted)
			accept_drop.arrival_predicate_true++;
	}
	if (SG_ACCEPT_DROP_LEGACY_A && accept_drop.boundary_capture_open)
	{
		if (recovery)
		{
			accept_drop.boundary_recovery_sampled = true;
			accept_drop.boundary_recovery_result = accepted;
		}
		else
		{
			accept_drop.boundary_arrival_sampled = true;
			accept_drop.boundary_arrival_result = accepted;
		}
	}
	if (accepted)
	{
		if (recovery)
			accept_drop.last_recovery = true;
		else
			accept_drop.last_arrival = true;
	}
	AcceptLogPrefix("predicate-result");
	sg_host.dprint(" link=%d kind=%s accepted=%d capture_open=%d "
	               "arrival_sampled=%d arrival_result=%d "
	               "recovery_sampled=%d recovery_result=%d\n",
	    accept_drop.link, kind ? kind : "?", accepted,
	    accept_drop.boundary_capture_open,
	    accept_drop.boundary_arrival_sampled,
	    accept_drop.boundary_arrival_result,
	    accept_drop.boundary_recovery_sampled,
	    accept_drop.boundary_recovery_result);
}

void SG_AcceptDropLegacyObserverBoundary(struct sg_bot_s *bot,
	int link_index, const edict_t *ent)
{
	const sg_accept_drop_selector_t *selector;
	sg_replay_observation_t observation;
	sg_replay_pose_t pose;
	sg_replay_status_t status;
	qboolean pending;
	qboolean was_observer_recovery;
	qboolean events_applied = false;

	if (!SG_AcceptDropLegacyAuthority(bot, link_index) || !ent)
		return;
	selector = &accept_selectors[accept_drop.requested_case - 1];
	pending = accept_drop.observer_active &&
	    accept_drop.observer.progress.step_pending;
	was_observer_recovery = accept_drop.observer.recovery;
	status = accept_drop.observer.progress.status;
	observation = AcceptObservation(ent);
	observation.drop_arrival_contact_clear =
	    accept_drop.boundary_arrival_sampled &&
	    accept_drop.boundary_arrival_result;
	observation.drop_recovery_contact_clear =
	    accept_drop.boundary_recovery_sampled &&
	    accept_drop.boundary_recovery_result;
	observation.drop_recovery_admitted =
	    !accept_drop.observer.spec.destination_water;
	observation.drop_landing_observed =
	    ent->groundentity != NULL || ent->waterlevel >= 2;
	if (pending)
	{
		/* Command four was deliberately retained for this boundary.  Fold in
		 * any real pusher/trigger/solid events raised after that command before
		 * consuming the one deferred observer snapshot. */
		if (accept_drop.observer_events_pending &&
		    accept_drop.observer_events_step == SG_DROP_LIVE_FRAME_STEPS - 1)
		{
			if (!SG_DropLiveEventsLatch(&accept_drop.observer_events,
			        bot->drop_live_events.contaminated,
			        bot->drop_live_events.door_passed))
				accept_drop.observer_events.contaminated = true;
			memset(&bot->drop_live_events, 0,
			       sizeof(bot->drop_live_events));
		}
		if (accept_drop.boundary_arrival_sampled)
		{
			accept_drop.observer_arrival_cached++;
			if (accept_drop.boundary_arrival_result)
				accept_drop.observer_arrival_cached_true++;
		}
		else
			accept_drop.observer_arrival_inferred++;
		if (accept_drop.boundary_recovery_sampled)
		{
			accept_drop.observer_recovery_cached++;
			if (accept_drop.boundary_recovery_result)
				accept_drop.observer_recovery_cached_true++;
		}
		else
			accept_drop.observer_recovery_inferred++;
		events_applied = AcceptObserverEventsApply(
		    SG_DROP_LIVE_FRAME_STEPS - 1, &observation);
		AcceptPoseFromEnt(ent, &pose);
		status = SG_DropReplayPostStep(&accept_drop.observer, &pose,
		    &observation);
		accept_drop.observer_poststeps++;
		accept_drop.observer_boundaries++;
		if (!was_observer_recovery && accept_drop.observer.recovery &&
		    accept_drop.observer_recovery_start_ms == 0)
			accept_drop.observer_recovery_start_ms =
			    accept_drop.observer.progress.elapsed_ms;
		if (bot->drop_recover && accept_drop.legacy_recovery_start_ms == 0)
			accept_drop.legacy_recovery_start_ms =
			    accept_drop.observer.progress.elapsed_ms;
		if (status != SG_REPLAY_RUNNING)
			accept_drop.observer_active = false;
	}
	AcceptPreContactBoundaryCloseLegacy(pending, status);
	AcceptLogPrefix("observer-boundary");
	sg_host.dprint(" link=%d pending=%d status=%d reason=%s elapsed_ms=%d "
	               "arrival_source=%s arrival_result=%d "
	               "recovery_source=%s recovery_result=%d "
	               "grounded=%d waterlevel=%d support_valid=%d events_applied=%d "
	               "contaminated=%d door_passed=%d active=%d "
	               "pre_contact_validated=%u pre_contact_sampled=%u "
	               "pre_contact_last_sampled=%u pre_contact_errors=%u\n",
	    link_index, pending, (int)status,
	    AcceptReplayReasonToken(accept_drop.observer.progress.reason),
	    accept_drop.observer.progress.elapsed_ms,
	    accept_drop.boundary_arrival_sampled ? "cached" :
	        "inferred-unsampled",
	    observation.drop_arrival_contact_clear,
	    accept_drop.boundary_recovery_sampled ? "cached" :
	        "inferred-unsampled",
	    observation.drop_recovery_contact_clear,
	    ent->groundentity != NULL, ent->waterlevel,
	    observation.ground_support_valid, events_applied, observation.contaminated,
	    observation.door_passed, accept_drop.observer_active,
	    accept_drop.pre_contact_validated, accept_drop.pre_contact_sampled,
	    accept_drop.pre_contact_last_sampled, accept_drop.pre_contact_errors);
	accept_drop.boundary_capture_open = false;
	if (selector->fixture_kind != SGAD_FIXTURE_NONE &&
	    accept_drop.injection_applied == 1)
	{
		accept_drop.checkpoint = selector->legacy_checkpoint;
		accept_drop.checkpoint_frame = level.framenum;
		if (accept_drop.injection_order_stage != SGAD_ORDER_SG_FRAME)
			accept_drop.injection_order_errors++;
		accept_drop.injection_order_stage = SGAD_ORDER_CHECKPOINT;
		accept_drop.stop_before_emit = selector->legacy_checkpoint ==
		    SGAD_CHECKPOINT_LEGACY_WET_RECOVERY;
	}
}

void SG_AcceptDropTrace(const char *kind, const edict_t *ent,
	const rune_link_t *link, const trace_t *trace, qboolean accepted)
{
	if (!AcceptContactOwns(ent, link))
		return;
	if (kind && strcmp(kind, "recovery") == 0)
	{
		accept_drop.recovery_traces++;
		if (accepted)
			accept_drop.recovery_trace_true++;
	}
	else
	{
		accept_drop.arrival_traces++;
		if (accepted)
			accept_drop.arrival_trace_true++;
	}
	if (kind && strcmp(kind, "recovery") == 0 && accepted)
		accept_drop.last_recovery = true;
	if ((!kind || strcmp(kind, "recovery") != 0) && accepted)
		accept_drop.last_arrival = true;
	AcceptLogPrefix("contact-trace");
	sg_host.dprint(" link=%d kind=%s startsolid=%d allsolid=%d fraction_bits=%08x "
	               "accepted=%d arrival_trace=%u recovery_trace=%u\n",
	    accept_drop.link, kind ? kind : "?",
	    trace ? (int)trace->startsolid : -1,
	    trace ? (int)trace->allsolid : -1,
	    trace ? AcceptFloatBits(trace->fraction) : 0, accepted,
	    accept_drop.arrival_traces, accept_drop.recovery_traces);
}

void SG_AcceptDropShelf(const struct sg_bot_s *bot, int link_index,
	const char *reason)
{
	if (!AcceptOwns(bot, link_index))
		return;
	accept_drop.shelves++;
	AcceptLogPrefix("shelf");
	sg_host.dprint(" link=%d reason=%s count=%u\n", link_index,
	    AcceptEventReasonToken(reason), accept_drop.shelves);
}

void SG_AcceptDropTeach(int link_index, const char *reason)
{
	if (!AcceptEnabled() || link_index != accept_drop.link)
		return;
	accept_drop.teaches++;
	AcceptLogPrefix("teach");
	sg_host.dprint(" link=%d reason=%s count=%u\n", link_index,
	    AcceptEventReasonToken(reason), accept_drop.teaches);
}
