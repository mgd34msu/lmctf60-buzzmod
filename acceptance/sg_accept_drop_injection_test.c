/* Host-free executable specification for the three injected DROP cases. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define SG_ACCEPT_DROP 1
#ifndef SG_ACCEPT_DROP_LEGACY_A
#define SG_ACCEPT_DROP_LEGACY_A 0
#endif
#include "slipgate/sg_accept_drop.c"

sg_host_t sg_host;
level_locals_t level;
static rune_t boundary_rune;
static rune_link_t boundary_links[24451];
static rune_seed_t boundary_seeds[960];
static edict_t boundary_edicts[2];
edict_t *g_edicts = boundary_edicts;
static const sg_accept_drop_selector_t *boundary_selector;
#ifdef SG_ACCEPT_DROP_CAPTURE_DPRINT
static gclient_t arm_client;
#endif
static cvar_t arm_gravity;
cvar_t *sv_gravity = &arm_gravity;
sg_bot_t sg_bots[SG_MAXBOTS];

static float BitsFloat(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

rune_t *SG_Rune(void)
{
	return &boundary_rune;
}

void SG_AcceptDropResetLifeActions(struct sg_bot_s *bot)
{
	(void)bot;
}

void ctf_hook_abort(edict_t *ent)
{
	(void)ent;
}

void ClientThink(edict_t *ent, usercmd_t *command)
{
	(void)command;
	if (ent)
		ent->groundentity = &boundary_edicts[0];
}

void SG_TimerArm(float *stamp, float delay)
{
	if (stamp)
		*stamp = level.time + delay;
}

qboolean SG_ImmutableSupport(edict_t *ent)
{
	return ent == boundary_edicts;
}

static void TestDprint(const char *format, ...)
{
#ifdef SG_ACCEPT_DROP_CAPTURE_DPRINT
	va_list arguments;

	va_start(arguments, format);
	AcceptEventCaptureV(format, arguments);
	va_end(arguments);
#else
	(void)format;
#endif
}

static trace_t BoundaryTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
	trace_t trace;

	(void)start;
	(void)mins;
	(void)maxs;
	(void)end;
	(void)passent;
	(void)contentmask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	return trace;
}

static int BoundaryPointContents(const vec3_t point)
{
	(void)point;
	return boundary_selector && boundary_selector->fixture_kind ==
	        SGAD_FIXTURE_WATER_DEPTH2 ? CONTENTS_WATER : 0;
}

static void BoundaryPmove(pmove_t *pm)
{
	vec3_t zero = { 0.0f, 0.0f, 0.0f };
	int axis;

	if (!pm || !boundary_selector)
		return;
	(void)pm->trace(zero, zero, zero, zero);
	pm->watertype = pm->pointcontents(zero);
	pm->waterlevel = boundary_selector->fixture_kind ==
	    SGAD_FIXTURE_WATER_DEPTH2 ? 2 : 0;
	pm->groundentity = boundary_selector->fixture_kind ==
	    SGAD_FIXTURE_DRY_SUPPORTED ? &boundary_edicts[0] : NULL;
	for (axis = 0; axis < 3; axis++)
	{
		pm->mins[axis] = axis == 2 ? -24.0f : -16.0f;
		pm->maxs[axis] = axis == 2 ? 32.0f : 16.0f;
	}
	pm->viewheight = 22.0f;
}

static void BoundaryLinkEntity(edict_t *ent)
{
	(void)ent;
}

typedef struct late_boundary_contact_s
{
	edict_t *ent;
	const rune_link_t *link;
	unsigned int arrival_calls;
	unsigned int recovery_calls;
	qboolean arrival_result;
	qboolean recovery_result;
} late_boundary_contact_t;

static qboolean LateBoundaryArrival(const sg_drop_replay_spec_t *spec,
	const sg_replay_pose_t *pose, void *context)
{
	late_boundary_contact_t *contact = context;

	(void)spec;
	(void)pose;
	contact->arrival_calls++;
	SG_AcceptDropCallback("arrival", contact->ent, contact->link);
	SG_AcceptDropPredicate("arrival", contact->ent, contact->link);
	SG_AcceptDropPredicateResult("arrival", contact->ent, contact->link,
	    contact->arrival_result);
	return contact->arrival_result;
}

static qboolean LateBoundaryRecovery(const sg_drop_replay_spec_t *spec,
	const sg_replay_pose_t *pose, void *context)
{
	late_boundary_contact_t *contact = context;

	(void)spec;
	(void)pose;
	contact->recovery_calls++;
	SG_AcceptDropCallback("recovery", contact->ent, contact->link);
	SG_AcceptDropPredicate("recovery", contact->ent, contact->link);
	SG_AcceptDropPredicateResult("recovery", contact->ent, contact->link,
	    contact->recovery_result);
	return contact->recovery_result;
}

static void FillInjectedState(sg_accept_drop_state_t *state,
	const sg_accept_drop_selector_t *selector, qboolean legacy)
{
	sg_accept_drop_checkpoint_t checkpoint = legacy ?
	    selector->legacy_checkpoint : selector->rev2_checkpoint;
	unsigned int deferrals = AcceptLateAirborneSelector(selector) ? 4U : 0U;
	unsigned int commands = deferrals ?
	    (deferrals + 1U) * SG_DROP_LIVE_FRAME_STEPS :
	    (checkpoint == SGAD_CHECKPOINT_REV2_RUNNING ? 5U : 4U);
	unsigned int boundaries = deferrals + 1U;
	int axis;

	memset(state, 0, sizeof(*state));
	state->started = true;
	state->action_begins = 1;
	state->historical_commands = commands;
	state->commands = commands;
	state->poses = commands;
	state->arm_poses = 1;
	state->pusher_begins = (boundaries + 1U) * 10U;
	state->pusher_ends = state->pusher_begins;
	state->injection_attempts = 1;
	state->injection_applied = 1;
	state->injection_pre_contact_captured = true;
	state->injection_deferrals = deferrals;
	state->injection_deferral_events = deferrals;
	state->injection_deferral_last_ordinal =
	    deferrals * SG_DROP_LIVE_FRAME_STEPS;
	state->injection_zero_ms = 1;
	state->injection_pmove_traces = 1;
	state->injection_pointcontents = 1;
	state->injection_boundary_checks = 1;
	state->injection_entity_passes = 1;
	state->injection_sg_frames = 1;
	state->private_stops = 1;
	state->injection_step = selector->injection_step;
	state->injection_frame = 100;
	state->checkpoint_frame = 101;
	state->injection_order_stage = SGAD_ORDER_CHECKPOINT;
	state->injection_fixture_seed = selector->fixture_seed;
	state->injection_pre_walkoff = true;
	state->injection_pre_airborne = selector->required_airborne;
	state->injection_terminal_geometry = selector->terminal_geometry;
	state->injection_recovery_geometry = selector->recovery_geometry;
	state->checkpoint = checkpoint;
	if (checkpoint == SGAD_CHECKPOINT_LEGACY_SHORT_CONTACT ||
	    checkpoint == SGAD_CHECKPOINT_REV2_SHORT_LANDING)
	{
		state->generic_handoff_begins = 1;
		state->generic_handoff_ends = 1;
		state->generic_handoff_completed_substeps =
		    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS;
		state->generic_handoff_begin_valid = true;
		state->generic_handoff_substeps =
		    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS;
		state->generic_handoff_total_msec =
		    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC;
	}
	for (axis = 0; axis < 3; axis++)
		state->injection_origin_bits[axis] =
		    AcceptFixturePmoveBits(selector->fixture_bits[axis]);
	if (selector->fixture_kind == SGAD_FIXTURE_DRY_SUPPORTED)
	{
		state->injection_grounded = true;
		state->injection_support_valid = true;
	}
	else
	{
		state->injection_watertype = CONTENTS_WATER;
		state->injection_waterlevel = 2;
	}
	state->injection_health = 100;
	state->injection_deadflag = 0;
	state->injection_movetype = MOVETYPE_WALK;
	state->injection_oldvelocity_zero = true;
	if (selector->fixture_boundary == SGAD_FIXTURE_BOUNDARY_POST_COMMAND)
	{
		state->injection_post_command_captures = 1;
		state->injection_post_command_validations = 1;
		state->post_command.captured = true;
		state->post_command.frame = state->injection_frame;
		state->post_command.step = selector->injection_step + 1;
		state->post_command.origin_bits[0] = UINT32_C(0xc12c0000);
		state->post_command.origin_bits[1] = UINT32_C(0x444fe000);
		state->post_command.origin_bits[2] = UINT32_C(0xc367e000);
		state->post_command.velocity_bits[0] = UINT32_C(0xc2918000);
		state->post_command.velocity_bits[1] = UINT32_C(0xc1910000);
		state->post_command.oldvelocity_bits[0] = UINT32_C(0);
		state->post_command.oldvelocity_bits[1] = UINT32_C(0);
		state->post_command.oldvelocity_bits[2] = UINT32_C(0);
		state->post_command.pmove_type = PM_NORMAL;
		state->post_command.health = 100;
		state->post_command.movetype = MOVETYPE_WALK;
		state->post_command.grounded = true;
		state->post_command.support_valid = true;
		state->post_command.terminal_geometry = selector->terminal_geometry;
		state->post_command.recovery_geometry = selector->recovery_geometry;
		state->post_command.historical_commands = 4;
		state->post_command.commands = 4;
		state->post_command.poses = 4;
		state->post_command.final_historical_matches = 4;
	}
	state->boundary_enters = boundaries;
	state->pre_contact_validated = deferrals;
	state->production_arrival_ms = SG_REPLAY_TIME_DISCOVER;

	if (legacy)
	{
		state->final_historical_matches = commands;
		state->observer_began = true;
		state->observer_presteps = commands;
		state->observer_poststeps = commands;
		state->observer_boundaries = boundaries;
		state->observer_command_matches = commands;
		state->observer.progress.elapsed_ms =
		    (int)(commands * SG_REPLAY_STEP_MS);
		state->observer.progress.arrival_ms = SG_REPLAY_TIME_DISCOVER;
		state->arrival_predicates = 1;
		state->arrival_predicate_results = 1;
		state->observer_arrival_cached = 1;
		state->observer_arrival_inferred = deferrals;
		state->observer_recovery_inferred = deferrals;
		state->saw_walkoff = true;
		if (checkpoint == SGAD_CHECKPOINT_LEGACY_WET_RECOVERY)
		{
			state->saw_airborne = true;
			state->saw_recovery = true;
			state->handoffs = 3;
			state->last_recovery = true;
			state->recovery_predicates = 1;
			state->recovery_predicate_results = 1;
			state->recovery_predicate_true = 1;
			state->recovery_traces = 1;
			state->recovery_trace_true = 1;
			state->observer_recovery_cached = 1;
			state->observer_recovery_cached_true = 1;
			state->legacy_recovery_start_ms =
			    (int)(commands * SG_REPLAY_STEP_MS);
			state->observer.progress.status = SG_REPLAY_FAILED;
			state->observer.progress.reason = SG_REPLAY_REASON_SHORT_LANDING;
		}
		else if (selector->required_airborne)
		{
			state->saw_airborne = true;
			state->handoffs = 2;
			state->shelves = 1;
			state->generic_handoffs = 1;
			state->recovery_predicates = 1;
			state->recovery_predicate_results = 1;
			state->observer_recovery_cached = 1;
			state->observer.progress.status = SG_REPLAY_FAILED;
			state->observer.progress.reason = SG_REPLAY_REASON_SHORT_LANDING;
		}
		else
		{
			state->handoffs = 1;
			state->shelves = 1;
			state->generic_handoffs = 1;
			state->observer_active = true;
			state->observer_recovery_inferred = 1;
			state->observer.progress.status = SG_REPLAY_RUNNING;
		}
	}
	else
	{
		state->boundary_exits = boundaries;
		state->boundary_results = boundaries;
		state->saw_walkoff = true;
		state->saw_airborne = selector->required_airborne;
		state->handoffs = selector->required_airborne ? 2 : 1;
		state->production_reason = SG_REPLAY_REASON_NONE;
		state->final_reason = SG_REPLAY_REASON_NONE;
		if (checkpoint == SGAD_CHECKPOINT_REV2_RUNNING)
		{
			state->production_status = SG_REPLAY_RUNNING;
			state->production_elapsed_ms = 125;
			state->final_outcome = SG_DROP_LIVE_RUNNING;
		}
		else
		{
			state->shelves = 1;
			state->generic_handoffs = 1;
			state->arrival_callbacks = 1;
			state->arrival_predicates = 1;
			state->arrival_predicate_results = 1;
			state->result_arrival_samples = 1;
			state->production_status = SG_REPLAY_FAILED;
			state->production_reason = SG_REPLAY_REASON_SHORT_LANDING;
			state->production_elapsed_ms =
			    (int)(commands * SG_REPLAY_STEP_MS);
			state->final_outcome = SG_DROP_LIVE_FAILED;
			state->final_reason = SG_REPLAY_REASON_SHORT_LANDING;
		}
	}
}

static void ExerciseGenericHooks(sg_accept_drop_state_t *state,
	const sg_accept_drop_selector_t *selector, qboolean legacy,
	int begin_substeps, int end_substeps, int begin_msec, int end_total_msec,
	int begin_calls, int end_calls)
{
	sg_bot_t bot;
	usercmd_t command;
	int call;

	FillInjectedState(state, selector, legacy);
	memset(&bot, 0, sizeof(bot));
	memset(&command, 0, sizeof(command));
	state->generic_handoffs = 0;
	state->generic_handoff_begins = 0;
	state->generic_handoff_ends = 0;
	state->generic_handoff_completed_substeps = 0;
	state->generic_handoff_pending = false;
	state->generic_handoff_begin_valid = false;
	state->generic_handoff_frame = 0;
	state->generic_handoff_bestlink = 0;
	state->generic_handoff_substeps = 0;
	state->generic_handoff_total_msec = 0;
	memset(&state->generic_handoff_command, 0,
	    sizeof(state->generic_handoff_command));
	state->phase = SGAD_ACTIVE;
	state->requested_case = (int)(selector - accept_selectors) + 1;
	state->armed = true;
	state->bot = &bot;
	state->link = selector->expected_link;
	bot.commit_link = -1;
	level.framenum = state->checkpoint_frame;
	sg_host.dprint = TestDprint;
	command.msec = (byte)begin_msec;
	accept_drop = *state;
	for (call = 0; call < begin_calls; call++)
		SG_AcceptDropGenericHandoffBegin(&bot, -1, &command,
		    begin_substeps);
	for (call = 0; call < end_calls; call++)
		SG_AcceptDropGenericHandoffEnd(&bot, -1, end_substeps,
		    end_total_msec);
	*state = accept_drop;
}

static int Expect(const char *name, const char *actual, const char *expected)
{
	if ((!actual && !expected) ||
	    (actual && expected && strcmp(actual, expected) == 0))
		return 1;
	fprintf(stderr, "%s: expected %s, got %s\n", name,
	    expected ? expected : "PASS", actual ? actual : "PASS");
	return 0;
}

static int ExerciseSupportedStep2Precondition(void)
{
	const sg_accept_drop_selector_t *selector = &accept_selectors[2];
	sg_accept_drop_state_t saved;
	sg_bot_t bot;
	int ok = 1;

	memset(&bot, 0, sizeof(bot));
	memset(&accept_drop, 0, sizeof(accept_drop));
	bot.commit_link = selector->expected_link;
	bot.drop_started = true;
	bot.drop_walkoff = true;
	bot.drop_replay_active = true;
	bot.drop_replay_link = selector->expected_link;
	bot.drop_replay.progress.status = SG_REPLAY_RUNNING;
	bot.drop_replay.progress.reason = SG_REPLAY_REASON_NONE;
	bot.drop_replay.progress.elapsed_ms = 75;
	bot.drop_replay.progress.step_pending = false;
	bot.drop_replay.walkoff = true;
	accept_drop.observer_began = true;
	accept_drop.observer_active = true;
	accept_drop.observer.progress.status = SG_REPLAY_RUNNING;
	accept_drop.observer.progress.reason = SG_REPLAY_REASON_NONE;
	accept_drop.observer.progress.elapsed_ms = 75;
	accept_drop.observer.progress.step_pending = false;
	accept_drop.observer.walkoff = true;
	accept_drop.observer_presteps = 3;
	accept_drop.observer_poststeps = 3;
	accept_drop.observer_command_matches = 3;
	accept_drop.action_begins = 1;
	accept_drop.historical_commands = 3;
	accept_drop.commands = 3;
	accept_drop.poses = 3;
	accept_drop.arm_poses = 1;
	accept_drop.final_historical_matches = 3;
	if (!AcceptInjectionPrecondition(&bot, selector->expected_link, 2,
	        selector))
		ok = 0;
	saved = accept_drop;
	if (AcceptInjectionPrecondition(&bot, selector->expected_link, 3,
	        selector))
		ok = 0;
	accept_drop = saved;
	bot.drop_replay.progress.step_pending = true;
	accept_drop.observer.progress.step_pending = true;
	if (AcceptInjectionPrecondition(&bot, selector->expected_link, 2,
	        selector))
		ok = 0;
	bot.drop_replay.progress.step_pending = false;
	accept_drop.observer.progress.step_pending = false;
	accept_drop.final_historical_matches = 2;
	if (AcceptInjectionPrecondition(&bot, selector->expected_link, 2,
	        selector))
		ok = 0;
	accept_drop = saved;
	bot.drop_airborne = true;
	accept_drop.observer.airborne = true;
	if (AcceptInjectionPrecondition(&bot, selector->expected_link, 2,
	        selector))
		ok = 0;
	if (!ok)
		fprintf(stderr, "supported step2 precondition matrix failed\n");
	return ok;
}

static void SetupLateCadence(const sg_accept_drop_selector_t *selector,
	unsigned int group, qboolean host_walkoff, qboolean host_airborne,
	qboolean recovery, sg_bot_t *bot, edict_t *ent, gclient_t *client)
{
	unsigned int commands = group * SG_DROP_LIVE_FRAME_STEPS;
	unsigned int deferrals = group - 1U;
	int clock_ms = (int)((commands - 1U) * SG_REPLAY_STEP_MS);

	memset(&accept_drop, 0, sizeof(accept_drop));
	memset(bot, 0, sizeof(*bot));
	memset(ent, 0, sizeof(*ent));
	memset(client, 0, sizeof(*client));
	ent->client = client;
	bot->ent = ent;
	bot->commit_link = selector->expected_link;
	bot->drop_started = true;
	bot->drop_walkoff = host_walkoff;
	bot->drop_airborne = host_airborne;
	bot->drop_recover = recovery;
	accept_drop.phase = SGAD_ACTIVE;
	accept_drop.requested_case = (int)(selector - accept_selectors) + 1;
	accept_drop.armed = true;
	accept_drop.started = true;
	accept_drop.bot = bot;
	accept_drop.link = selector->expected_link;
	accept_drop.action_begins = 1;
	accept_drop.historical_commands = commands;
	accept_drop.commands = commands;
	accept_drop.poses = commands;
	accept_drop.arm_poses = 1;
	accept_drop.final_historical_matches = commands;
	accept_drop.injection_deferrals = deferrals;
	accept_drop.injection_deferral_events = deferrals;
	accept_drop.injection_deferral_last_ordinal =
	    deferrals * SG_DROP_LIVE_FRAME_STEPS;
	accept_drop.boundary_enters = deferrals;
	accept_drop.pre_contact_validated = deferrals;
	if (SG_ACCEPT_DROP_LEGACY_A)
	{
		accept_drop.observer_began = true;
		accept_drop.observer_active = true;
		accept_drop.observer.progress.status = SG_REPLAY_RUNNING;
		accept_drop.observer.progress.reason = SG_REPLAY_REASON_NONE;
		accept_drop.observer.progress.elapsed_ms = clock_ms;
		accept_drop.observer.progress.arrival_ms = SG_REPLAY_TIME_DISCOVER;
		accept_drop.observer.progress.step_pending = true;
		accept_drop.observer.spec.heading = selector->heading;
		accept_drop.observer.spec.destination_water =
		    (selector->destination_flags & RSF_WATER) != 0;
		accept_drop.observer.spec.expected_arrival_ms = selector->cost_ms;
		accept_drop.observer.walkoff = host_walkoff;
		accept_drop.observer.airborne = host_airborne;
		accept_drop.observer.recovery = recovery;
		accept_drop.observer_presteps = commands;
		accept_drop.observer_poststeps = commands - 1U;
		accept_drop.observer_boundaries = deferrals;
		accept_drop.observer_command_matches = commands;
		accept_drop.observer_arrival_inferred = deferrals;
		accept_drop.observer_recovery_inferred = deferrals;
		accept_drop.observer_events_pending = true;
		accept_drop.observer_events_step = SG_DROP_LIVE_FRAME_STEPS - 1;
	}
	else
	{
		accept_drop.boundary_exits = deferrals;
		accept_drop.boundary_results = deferrals;
		bot->drop_replay_active = true;
		bot->drop_replay_link = selector->expected_link;
		bot->drop_replay.progress.status = SG_REPLAY_RUNNING;
		bot->drop_replay.progress.reason = SG_REPLAY_REASON_NONE;
		bot->drop_replay.progress.elapsed_ms = clock_ms;
		bot->drop_replay.progress.arrival_ms = SG_REPLAY_TIME_DISCOVER;
		bot->drop_replay.progress.step_pending = true;
		bot->drop_replay.spec.heading = selector->heading;
		bot->drop_replay.spec.destination_water =
		    (selector->destination_flags & RSF_WATER) != 0;
		bot->drop_replay.spec.expected_arrival_ms = selector->cost_ms;
		bot->drop_replay.walkoff = host_walkoff;
		bot->drop_replay.airborne = host_airborne;
		bot->drop_replay.recovery = recovery;
	}
	sg_host.dprint = TestDprint;
}

static void PrepareBoundaryFixture(const sg_accept_drop_selector_t *selector,
	edict_t *ent, gclient_t *client)
{
	int axis;

	memset(boundary_seeds, 0, sizeof(boundary_seeds));
	boundary_rune.hdr.num_seeds =
	    (int)(sizeof(boundary_seeds) / sizeof(boundary_seeds[0]));
	boundary_rune.seeds = boundary_seeds;
	boundary_rune.v3_header.gravity = 800.0f;
	for (axis = 0; axis < 3; axis++)
	{
		boundary_seeds[selector->fixture_seed].origin[axis] =
		    BitsFloat(selector->fixture_bits[axis]);
		boundary_seeds[selector->destination].origin[axis] =
		    BitsFloat(selector->destination_bits[axis]);
		ent->s.origin[axis] = BitsFloat(selector->source_bits[axis]);
		ent->s.old_origin[axis] = ent->s.origin[axis];
		client->v_angle[axis] = 10.0f + axis * 7.0f;
		client->ps.pmove.delta_angles[axis] = (short)(axis + 1);
		client->oldvelocity[axis] = 123.0f + axis;
	}
	client->ps.pmove.pm_type = PM_NORMAL;
	client->ps.pmove.gravity = 800;
	ent->health = 100;
	ent->movetype = MOVETYPE_WALK;
	ent->groundentity = NULL;
	ent->waterlevel = 0;
	ent->watertype = 0;
	boundary_edicts[0].inuse = true;
	boundary_edicts[0].linkcount = 41;
	boundary_edicts[0].classname = "worldspawn";
	boundary_selector = selector;
	sg_host.trace = BoundaryTrace;
	sg_host.pointcontents = BoundaryPointContents;
	sg_host.pmove = BoundaryPmove;
	sg_host.linkentity = BoundaryLinkEntity;
}

static int RunHookOrderedNextCommandGroup(const sg_accept_drop_selector_t *selector,
	sg_bot_t *bot, edict_t *ent)
{
	usercmd_t command;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_drop_live_events_t host_events;
	qboolean source_door_pending = false;
	int step;

	for (step = 0; step < SG_DROP_LIVE_FRAME_STEPS; step++)
	{
		memset(&command, 0, sizeof(command));
		memset(&host_events, 0, sizeof(host_events));
		AcceptPoseFromEnt(ent, &pose);
		if (SG_ACCEPT_DROP_LEGACY_A)
		{
			sg_drop_replay_state_t predictor = accept_drop.observer;

			if (!SG_AcceptDropObserverBeginCommand(bot,
			        selector->expected_link, &host_events,
			        &source_door_pending))
				return 0;
			command.msec = SG_REPLAY_STEP_MS;
			if (SG_DropReplayPreStep(&predictor, &pose, &command) !=
			    SG_REPLAY_RUNNING)
				return 0;
		}
		else if (SG_DropReplayPreStep(&bot->drop_replay, &pose, &command) !=
		         SG_REPLAY_RUNNING)
			return 0;
		SG_AcceptDropCommandHistorical(bot, selector->expected_link, step,
		    &command);
		SG_AcceptDropCommand(bot, selector->expected_link, step, &command);
		SG_AcceptDropPose(bot, selector->expected_link, step, ent);
		if (SG_ACCEPT_DROP_LEGACY_A)
			SG_AcceptDropObserverTakeEvents(bot, selector->expected_link,
			    step, &host_events);
		if (!SG_ACCEPT_DROP_LEGACY_A && step < SG_DROP_LIVE_FRAME_STEPS - 1)
		{
			observation = AcceptObservation(ent);
			if (SG_DropReplayPostStep(&bot->drop_replay, &pose,
			        &observation) != SG_REPLAY_RUNNING)
				return 0;
		}
		if (SG_AcceptDropAfterStep(bot, selector->expected_link, step, ent))
			return 0;
	}
	return accept_drop.injection_attempts == 1 &&
	       accept_drop.injection_applied == 1 &&
	       accept_drop.injection_order_stage == SGAD_ORDER_INJECTED &&
	       accept_drop.injection_frame == level.framenum &&
	       accept_drop.injection_step == selector->injection_step &&
	       accept_drop.injection_fixture_seed == selector->fixture_seed;
}

static void RunBoundaryPushers(void)
{
	int index;

	for (index = 0; index < 10; index++)
	{
		SG_AcceptDropPusher(&boundary_edicts[0], "begin");
		SG_AcceptDropPusher(&boundary_edicts[0], "end");
	}
}

static int FinishHookOrderedInjectedSequence(
	const sg_accept_drop_selector_t *selector, sg_bot_t *bot, edict_t *ent)
{
	sg_replay_pose_t pose;
	sg_drop_live_events_t events;
	sg_drop_live_result_t result;
	late_boundary_contact_t contact;
	trace_t trace;
	usercmd_t handoff;
	rune_link_t *link = &boundary_links[selector->expected_link];

	level.framenum = accept_drop.injection_frame + 1;
	RunBoundaryPushers();
	SG_AcceptDropEntityPass();
	SG_AcceptDropFrameBegin();
	SG_AcceptDropBoundary(bot, selector->expected_link,
	    SG_ACCEPT_DROP_LEGACY_A ? "boundary-enter-legacy" :
	                              "boundary-enter-rev2",
	    NULL, NULL);
	if (SG_ACCEPT_DROP_LEGACY_A)
	{
		SG_AcceptDropPredicate("arrival", ent, link);
		SG_AcceptDropPredicateResult("arrival", ent, link, false);
		SG_AcceptDropPredicate("recovery", ent, link);
		if (selector->legacy_checkpoint == SGAD_CHECKPOINT_LEGACY_WET_RECOVERY)
		{
			memset(&trace, 0, sizeof(trace));
			trace.fraction = 1.0f;
			SG_AcceptDropTrace("recovery", ent, link, &trace, true);
			SG_AcceptDropPredicateResult("recovery", ent, link, true);
			bot->drop_recover = true;
		}
		else
			SG_AcceptDropPredicateResult("recovery", ent, link, false);
		SG_AcceptDropLegacyObserverBoundary(bot, selector->expected_link, ent);
		if (selector->legacy_checkpoint == SGAD_CHECKPOINT_LEGACY_WET_RECOVERY)
		{
			if (!SG_AcceptDropStopBeforeEmit(bot, selector->expected_link))
				return 0;
		}
		else
			SG_AcceptDropShelf(bot, selector->expected_link,
			    "short landing");
	}
	else
	{
		memset(&events, 0, sizeof(events));
		memset(&contact, 0, sizeof(contact));
		contact.ent = ent;
		contact.link = link;
		AcceptPoseFromEnt(ent, &pose);
		result = SG_DropLiveBoundary(&bot->drop_replay,
		    &bot->drop_replay_active, &bot->drop_replay_link,
		    selector->expected_link, &pose, AcceptSupportValid(ent), &events,
		    LateBoundaryArrival, LateBoundaryRecovery, &contact);
		SG_AcceptDropBoundary(bot, selector->expected_link,
		    "boundary-exit-rev2", &result, &events);
		if (result.outcome != SG_DROP_LIVE_FAILED ||
		    result.replay_reason != SG_REPLAY_REASON_SHORT_LANDING ||
		    contact.arrival_calls != 1 || contact.recovery_calls != 0 ||
		    accept_drop.injection_applied != 1 ||
		    accept_drop.checkpoint != SGAD_CHECKPOINT_REV2_SHORT_LANDING)
			return 0;
		SG_AcceptDropShelf(bot, selector->expected_link, "short landing");
	}
	if (accept_drop.checkpoint == SGAD_CHECKPOINT_LEGACY_SHORT_CONTACT ||
	    accept_drop.checkpoint == SGAD_CHECKPOINT_REV2_SHORT_LANDING)
	{
		memset(&handoff, 0, sizeof(handoff));
		handoff.msec = SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC;
		bot->commit_link = -1;
		SG_AcceptDropGenericHandoffBegin(bot, -1, &handoff,
		    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS);
		SG_AcceptDropGenericHandoffEnd(bot, -1,
		    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
		    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC);
	}
	SG_AcceptDropAfterBot(bot);
	return accept_drop.finished && accept_drop.phase == SGAD_FINISHED &&
	       accept_drop.finish_diagnostic &&
	       strcmp(accept_drop.finish_diagnostic, "none") == 0;
}

static int ExerciseHookOrderedLateBoundary(const sg_accept_drop_selector_t *selector,
	int waterlevel, qboolean arrival_result, qboolean expect_apply,
	sg_replay_reason_t terminal_reason)
{
	sg_bot_t bot;
	edict_t ent;
	gclient_t client;
	sg_replay_pose_t pose;
	sg_drop_live_events_t events;
	sg_drop_live_result_t result;
	late_boundary_contact_t contact;
	unsigned int group = 5;
	sg_accept_drop_injection_decision_t next_decision;
	int ok = 1;

	memset(&boundary_rune, 0, sizeof(boundary_rune));
	memset(boundary_links, 0, sizeof(boundary_links));
	boundary_rune.links = boundary_links;
	boundary_rune.hdr.num_links =
	    (int)(sizeof(boundary_links) / sizeof(boundary_links[0]));
	SetupLateCadence(selector, group, true, true, false, &bot, &ent, &client);
	accept_drop.pusher_begins = group * 10U;
	accept_drop.pusher_ends = group * 10U;
	if (SG_ACCEPT_DROP_LEGACY_A)
		accept_drop.observer.airborne = false;
	else
		bot.drop_replay.airborne = false;
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_DEFER)
		ok = 0;
	AcceptRecordInjectionDeferral(&bot, selector->expected_link, 3, selector);
	RunBoundaryPushers();
	SG_AcceptDropBoundary(&bot, selector->expected_link,
	    SG_ACCEPT_DROP_LEGACY_A ? "boundary-enter-legacy" :
	                              "boundary-enter-rev2",
	    NULL, NULL);
	memset(&pose, 0, sizeof(pose));
	memset(&events, 0, sizeof(events));
	memset(&contact, 0, sizeof(contact));
	pose.origin[0] = selector->source_bits[0] ? -1000.0f : -900.0f;
	pose.grounded = false;
	pose.waterlevel = waterlevel;
	pose.watertype = waterlevel ? CONTENTS_WATER : 0;
	ent.waterlevel = waterlevel;
	ent.watertype = pose.watertype;
	contact.ent = &ent;
	contact.link = &boundary_links[selector->expected_link];
	contact.arrival_result = arrival_result;
	if (SG_ACCEPT_DROP_LEGACY_A)
	{
		SG_AcceptDropLegacyObserverBoundary(&bot, selector->expected_link,
		    &ent);
		if ((expect_apply &&
		     (!accept_drop.observer.airborne || !accept_drop.observer_active ||
		      accept_drop.observer.progress.status != SG_REPLAY_RUNNING ||
		      accept_drop.pre_contact_errors != 0)) ||
		    (!expect_apply &&
		     (accept_drop.observer_active ||
		      accept_drop.observer.progress.status != SG_REPLAY_FAILED ||
		      accept_drop.observer.progress.reason != terminal_reason ||
		      accept_drop.pre_contact_errors != 1)) ||
		    accept_drop.observer_arrival_inferred != group ||
		    accept_drop.arrival_predicates != 0 ||
		    accept_drop.recovery_predicates != 0)
			ok = 0;
	}
	else
	{
		result = SG_DropLiveBoundary(&bot.drop_replay,
		    &bot.drop_replay_active, &bot.drop_replay_link,
		    selector->expected_link, &pose, false, &events,
		    LateBoundaryArrival, LateBoundaryRecovery, &contact);
		SG_AcceptDropBoundary(&bot, selector->expected_link,
		    "boundary-exit-rev2", &result, &events);
		if ((expect_apply &&
		     (result.outcome != SG_DROP_LIVE_RUNNING ||
		      !bot.drop_replay_active ||
		      bot.drop_replay.progress.status != SG_REPLAY_RUNNING ||
		      accept_drop.pre_contact_errors != 0)) ||
		    (!expect_apply && !arrival_result &&
		     (result.outcome != SG_DROP_LIVE_FAILED ||
		      result.replay_reason != terminal_reason || bot.drop_replay_active ||
		      accept_drop.pre_contact_errors != 1)) ||
		    (!expect_apply && arrival_result &&
		     (result.outcome != SG_DROP_LIVE_RUNNING ||
		      result.replay_reason != terminal_reason || !bot.drop_replay_active ||
		      bot.drop_replay.progress.status != SG_REPLAY_RUNNING ||
		      accept_drop.pre_contact_errors != 1)) ||
		    !result.arrival_sampled ||
		    result.arrived != arrival_result ||
		    result.recovery_sampled || !bot.drop_replay.airborne ||
		    contact.arrival_calls != 1 || contact.recovery_calls != 0 ||
		    accept_drop.arrival_callbacks != 1 ||
		    accept_drop.arrival_predicates != 1 ||
		    accept_drop.arrival_predicate_results != 1 ||
		    accept_drop.result_arrival_samples != 1)
			ok = 0;
		if (expect_apply)
		{
			/* A normal pre-injection boundary must not satisfy the selector's
			 * terminal checkpoint merely because it produced a result. */
			if (accept_drop.checkpoint != SGAD_CHECKPOINT_NONE ||
			    accept_drop.finished)
				ok = 0;
			SG_AcceptDropAfterBot(&bot);
			if (accept_drop.checkpoint != SGAD_CHECKPOINT_NONE ||
			    accept_drop.finished || accept_drop.phase != SGAD_ACTIVE)
				ok = 0;
		}
	}
	if (expect_apply)
	{
		PrepareBoundaryFixture(selector, &ent, &client);
		level.framenum = 100;
		if (!RunHookOrderedNextCommandGroup(selector, &bot, &ent))
			ok = 0;
		else if (!FinishHookOrderedInjectedSequence(selector, &bot, &ent))
			ok = 0;
		next_decision = accept_drop.injection_applied ? SGAD_INJECTION_APPLY :
		                                                  SGAD_INJECTION_INVALID;
	}
	else
	{
		next_decision = AcceptLateAirborneDecision(&bot,
		    selector->expected_link, 3, selector);
		if (next_decision != SGAD_INJECTION_INVALID)
			ok = 0;
	}
	if (!ok)
		fprintf(stderr, "hook-ordered late boundary sequence failed selector=%s "
		                "variant=%s waterlevel=%d decision=%d arrival=%u recovery=%u "
		                "inferred=%u/%u observer=%d/%d/%d/%d "
		                "boundary=%u/%u/%u elapsed=%d pending=%d "
		                "authority=%d/%d/%d status=%d reason=%d\n",
		    selector->name, AcceptVariant(), waterlevel, (int)next_decision,
		    contact.arrival_calls, contact.recovery_calls,
		    accept_drop.observer_arrival_inferred,
		    accept_drop.observer_recovery_inferred,
		    accept_drop.observer_presteps, accept_drop.observer_poststeps,
		    accept_drop.observer_boundaries,
		    accept_drop.observer_command_matches, accept_drop.boundary_enters,
		    accept_drop.boundary_exits, accept_drop.boundary_results,
		    SG_ACCEPT_DROP_LEGACY_A ? accept_drop.observer.progress.elapsed_ms :
		                               bot.drop_replay.progress.elapsed_ms,
		    SG_ACCEPT_DROP_LEGACY_A ? accept_drop.observer.progress.step_pending :
		                               bot.drop_replay.progress.step_pending,
		    SG_ACCEPT_DROP_LEGACY_A ? accept_drop.observer.walkoff :
		                               bot.drop_replay.walkoff,
		    SG_ACCEPT_DROP_LEGACY_A ? accept_drop.observer.airborne :
		                               bot.drop_replay.airborne,
		    SG_ACCEPT_DROP_LEGACY_A ? accept_drop.observer.recovery :
		                               bot.drop_replay.recovery,
		    SG_ACCEPT_DROP_LEGACY_A ? (int)accept_drop.observer.progress.status :
		                               (int)bot.drop_replay.progress.status,
		    SG_ACCEPT_DROP_LEGACY_A ? (int)accept_drop.observer.progress.reason :
		                               (int)bot.drop_replay.progress.reason);
	if (!ok)
		fprintf(stderr, "finish phase=%d finished=%d diagnostic=%s checkpoint=%d "
		                "pushers=%u/%u private=%u shelf=%u handoff=%u/%u/%u "
		                "order=%d/%u/%u/%u boundary-checks=%u\n",
		    (int)accept_drop.phase, accept_drop.finished,
		    accept_drop.finish_diagnostic ? accept_drop.finish_diagnostic : "?",
		    (int)accept_drop.checkpoint, accept_drop.pusher_begins,
		    accept_drop.pusher_ends, accept_drop.private_stops,
		    accept_drop.shelves, accept_drop.generic_handoffs,
		    accept_drop.generic_handoff_begins,
		    accept_drop.generic_handoff_ends,
		    (int)accept_drop.injection_order_stage,
		    accept_drop.injection_order_errors,
		    accept_drop.injection_entity_passes,
		    accept_drop.injection_sg_frames,
		    accept_drop.injection_boundary_checks);
	if (!ok && !SG_ACCEPT_DROP_LEGACY_A)
		fprintf(stderr, "hook-ordered result outcome=%d failure=%d reason=%d sampled=%d "
		                "arrived=%d active=%d errors=%u predicate_true=%u\n",
		    (int)result.outcome, (int)result.failure,
		    (int)result.replay_reason, result.arrival_sampled, result.arrived,
		    bot.drop_replay_active, accept_drop.pre_contact_errors,
		    accept_drop.arrival_predicate_true);
	return ok;
}

static int ExerciseLateAirborneDecision(
	const sg_accept_drop_selector_t *selector)
{
	sg_accept_drop_state_t saved;
	sg_bot_t bot;
	edict_t ent;
	gclient_t client;
	int ok = 1;

	SetupLateCadence(selector, 6, true, true, false, &bot, &ent, &client);
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_APPLY)
		ok = 0;

	/* Host first becomes airborne on this pending step 3, but the observer or
	 * reducer has not latched it.  This must defer one more complete group. */
	SetupLateCadence(selector, 5, true, true, false, &bot, &ent, &client);
	if (SG_ACCEPT_DROP_LEGACY_A)
		accept_drop.observer.airborne = false;
	else
		bot.drop_replay.airborne = false;
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_DEFER)
		ok = 0;
	AcceptRecordInjectionDeferral(&bot, selector->expected_link, 3, selector);
	if (accept_drop.injection_deferrals != 5 ||
	    accept_drop.injection_deferral_events != 5 ||
	    accept_drop.injection_deferral_last_ordinal != 20)
		ok = 0;

	SetupLateCadence(selector, 1, false, false, false, &bot, &ent, &client);
	AcceptRecordInjectionDeferral(&bot, selector->expected_link, 3, selector);
	if (accept_drop.injection_deferrals != 1 ||
	    accept_drop.injection_deferral_events != 1 ||
	    accept_drop.injection_deferral_last_ordinal != 4 ||
	    accept_drop.injection_attempts != 0 ||
	    accept_drop.injection_applied != 0)
		ok = 0;

	SetupLateCadence(selector, 15, false, false, false, &bot, &ent, &client);
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;
	SetupLateCadence(selector, 15, true, true, false, &bot, &ent, &client);
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_APPLY)
		ok = 0;
	SetupLateCadence(selector, 6, true, true, true, &bot, &ent, &client);
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;
	SetupLateCadence(selector, 6, false, true, false, &bot, &ent, &client);
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;
	SetupLateCadence(selector, 6, false, false, false, &bot, &ent, &client);
	if (SG_ACCEPT_DROP_LEGACY_A)
		accept_drop.observer.walkoff = true;
	else
		bot.drop_replay.walkoff = true;
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;
	SetupLateCadence(selector, 6, true, false, false, &bot, &ent, &client);
	if (SG_ACCEPT_DROP_LEGACY_A)
		accept_drop.observer.airborne = true;
	else
		bot.drop_replay.airborne = true;
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;
	SetupLateCadence(selector, 6, true, true, false, &bot, &ent, &client);
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 2,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;
	saved = accept_drop;
	accept_drop.commands--;
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;
	accept_drop = saved;
	accept_drop.injection_deferral_events--;
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;
	accept_drop = saved;
	accept_drop.injection_deferral_events++;
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;
	accept_drop = saved;
	accept_drop.boundary_enters--;
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;
	accept_drop = saved;
	accept_drop.arrival_predicates = 1;
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;
	accept_drop = saved;
	if (SG_ACCEPT_DROP_LEGACY_A)
		accept_drop.observer.progress.elapsed_ms--;
	else
		bot.drop_replay.progress.elapsed_ms--;
	if (AcceptLateAirborneDecision(&bot, selector->expected_link, 3,
	        selector) != SGAD_INJECTION_INVALID)
		ok = 0;

	if (!ok)
		fprintf(stderr, "late-airborne decision matrix failed selector=%s "
		                "variant=%s\n", selector->name, AcceptVariant());
	return ok;
}

int main(void)
{
	sg_accept_drop_state_t base, changed;
	int case_index;
	int legacy;
	int ok = 1;

	ok &= ExerciseSupportedStep2Precondition();
	ok &= ExerciseLateAirborneDecision(&accept_selectors[3]);
	ok &= ExerciseLateAirborneDecision(&accept_selectors[4]);
	ok &= ExerciseHookOrderedLateBoundary(&accept_selectors[3], 0, false, true,
	    SG_REPLAY_REASON_NONE);
	ok &= ExerciseHookOrderedLateBoundary(&accept_selectors[4], 0, false, true,
	    SG_REPLAY_REASON_NONE);
	/* The dry depth selector also survives waterlevel one without a contact;
	 * the wet selector must fail shallow-water policy and can never apply. */
	ok &= ExerciseHookOrderedLateBoundary(&accept_selectors[4], 1, false, true,
	    SG_REPLAY_REASON_NONE);
	ok &= ExerciseHookOrderedLateBoundary(&accept_selectors[3], 1, false, false,
	    SG_REPLAY_REASON_SHALLOW_WATER_CONTACT);
	if (!SG_ACCEPT_DROP_LEGACY_A)
		ok &= ExerciseHookOrderedLateBoundary(&accept_selectors[4], 0, true, false,
		    SG_REPLAY_REASON_NONE);

	for (case_index = SG_ACCEPT_DROP_NATURAL_CASE_COUNT;
	     case_index < SG_ACCEPT_DROP_CASE_COUNT; case_index++)
		for (legacy = 0; legacy <= 1; legacy++)
		{
			const sg_accept_drop_selector_t *selector =
			    &accept_selectors[case_index];

			FillInjectedState(&base, selector, legacy);
			ok &= Expect("positive",
			    AcceptInjectedFinishFailure(&base, selector, legacy), NULL);

			if (AcceptLateAirborneSelector(selector))
			{
				changed = base;
				changed.injection_pre_contact_captured = false;
				ok &= Expect("pre-contact-snapshot-missing",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-contact-history");

				changed = base;
				changed.injection_deferral_events--;
				ok &= Expect("deferral-event-missing",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-count");

				changed = base;
				changed.injection_deferral_events++;
				ok &= Expect("deferral-event-duplicate",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-count");

				changed = base;
				changed.injection_deferral_last_ordinal -=
				    SG_DROP_LIVE_FRAME_STEPS;
				ok &= Expect("deferral-order",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-count");

				changed = base;
				changed.boundary_enters--;
				ok &= Expect("deferral-boundary-count",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-boundary-count");

				changed = base;
				changed.pre_contact_errors = 1;
				ok &= Expect("pre-contact-ledger-error",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-contact-history");

				if (!legacy)
				{
					changed = base;
					changed.injection_pre_arrival_samples = 1;
					changed.pre_contact_sampled = 1;
					changed.pre_contact_last_sampled =
					    changed.injection_deferrals;
					changed.arrival_callbacks++;
					changed.arrival_predicates++;
					changed.arrival_predicate_results++;
					changed.result_arrival_samples++;
					ok &= Expect("pre-contact-sampled-positive",
					    AcceptInjectedFinishFailure(&changed, selector, legacy),
					    NULL);

					changed.pre_contact_last_sampled--;
					ok &= Expect("pre-contact-sampled-not-last",
					    AcceptInjectedFinishFailure(&changed, selector, legacy),
					    "finish-contact-history");

					changed = base;
					changed.injection_pre_arrival_samples = 2;
					changed.pre_contact_sampled = 2;
					changed.pre_contact_last_sampled =
					    changed.injection_deferrals;
					changed.arrival_callbacks += 2;
					changed.arrival_predicates += 2;
					changed.arrival_predicate_results += 2;
					changed.result_arrival_samples += 2;
					ok &= Expect("pre-contact-sampled-duplicate",
					    AcceptInjectedFinishFailure(&changed, selector, legacy),
					    "finish-contact-history");
				}
			}

			changed = base;
			changed.injection_applied = 0;
			ok &= Expect("injection-count",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-injection-count");

			changed = base;
			changed.injection_origin_bits[0] ^= UINT32_C(1);
			ok &= Expect("fixture-bits",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-injection-fixture");

			changed = base;
			changed.injection_health = 0;
			ok &= Expect("fixture-health",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-injection-fixture");

			changed = base;
			changed.injection_oldvelocity_zero = false;
			ok &= Expect("fixture-oldvelocity",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-injection-fixture");

			if (selector->fixture_boundary ==
			    SGAD_FIXTURE_BOUNDARY_POST_COMMAND)
			{
				changed = base;
				changed.injection_post_command_captures = 0;
				ok &= Expect("post-command-capture-missing",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-order");

				changed = base;
				changed.injection_post_command_captures = 2;
				ok &= Expect("post-command-capture-duplicate",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-order");

				changed = base;
				changed.post_command.health = 0;
				ok &= Expect("post-command-capture-health",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-fixture");
			}

			changed = base;
			changed.checkpoint_frame++;
			ok &= Expect("frame-order",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-injection-order");

			changed = base;
			changed.commands++;
			ok &= Expect("command-count",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-command-count");

			changed = base;
			changed.pusher_order_errors = 1;
			ok &= Expect("pusher-order",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-pusher-order");

			changed = base;
			changed.pusher_begins -= 10;
			changed.pusher_ends -= 10;
			ok &= Expect("pusher-exact-count",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-pusher-order");

			changed = base;
			changed.private_stops = 0;
			ok &= Expect("private-stop",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-private-stop");

			changed = base;
			changed.generic_handoffs ^= 1U;
			ok &= Expect("generic-handoff",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-injection-checkpoint");

			if (base.generic_handoffs == 1)
			{
				ExerciseGenericHooks(&changed, selector, legacy,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC, 1, 1);
				ok &= Expect("handoff-public-eight-positive",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    NULL);

				ExerciseGenericHooks(&changed, selector, legacy, 4, 4,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC, 1, 1);
				ok &= Expect("handoff-four-rejected",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-count");

				ExerciseGenericHooks(&changed, selector, legacy, 7, 7,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC, 1, 1);
				ok &= Expect("handoff-seven-rejected",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-count");

				ExerciseGenericHooks(&changed, selector, legacy,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC, 1, 0);
				ok &= Expect("handoff-missing-end-public",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-checkpoint");

				ExerciseGenericHooks(&changed, selector, legacy,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC, 0, 1);
				ok &= Expect("handoff-end-before-begin",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-count");

				ExerciseGenericHooks(&changed, selector, legacy,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC, 2, 1);
				ok &= Expect("handoff-duplicate-begin",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-count");

				ExerciseGenericHooks(&changed, selector, legacy,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC, 1, 2);
				ok &= Expect("handoff-duplicate-end",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-count");

				ExerciseGenericHooks(&changed, selector, legacy,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_SUBSTEPS,
				    SG_ACCEPT_DROP_GENERIC_HANDOFF_MSEC, 99, 1, 1);
				ok &= Expect("handoff-wrong-total",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-count");

				changed = base;
				changed.generic_handoffs = 0;
				changed.generic_handoff_ends = 0;
				changed.generic_handoff_completed_substeps = 0;
				changed.generic_handoff_pending = true;
				ok &= Expect("handoff-begin-without-end",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-checkpoint");

				changed = base;
				changed.generic_handoff_completed_substeps = 7;
				ok &= Expect("handoff-seven-state",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-checkpoint");

				changed = base;
				changed.generic_handoff_completed_substeps = 4;
				ok &= Expect("handoff-four-state",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-checkpoint");

				changed = base;
				changed.generic_handoff_total_msec = 99;
				ok &= Expect("handoff-total-state",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-checkpoint");

				changed = base;
				changed.generic_handoffs = 2;
				changed.generic_handoff_begins = 2;
				changed.generic_handoff_ends = 2;
				changed.generic_handoff_completed_substeps = 16;
				ok &= Expect("handoff-duplicate-end-order",
				    AcceptInjectedFinishFailure(&changed, selector, legacy),
				    "finish-injection-checkpoint");
			}

			changed = base;
			changed.arrival_predicate_true = 1;
			ok &= Expect("false-arrival",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-arrival-evidence");

			changed = base;
			changed.injection_pre_airborne = !selector->required_airborne;
			ok &= Expect("pre-airborne",
			    AcceptInjectedFinishFailure(&changed, selector, legacy),
			    "finish-injection-precondition");
		}

	if (ok)
		printf("injection-selftest ok cases=3 variants=2 matrix=complete\n");
	return ok ? 0 : 1;
}
