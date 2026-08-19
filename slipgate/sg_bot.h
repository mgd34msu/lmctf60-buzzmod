/*
 * sg_bot.h -- the bot slot: the one structure SLIPGATE hangs a bot on.
 *
 * Split out of sg_arach.c in the 2026-08-11 standards pass so the
 * roster lifecycle (sg_client.c) and the think pipeline (sg_arach.c)
 * share the type through a header instead of one 10,800-line file.
 */
#ifndef SG_BOT_H
#define SG_BOT_H

#include "sg_drop_live.h"
#include "sg_hook_live.h"
#include "sg_hook_diagnostics.h"
#include "sg_compound_guard.h"
#include "sg_human_speed.h"
#include "sg_door_approach.h"
#include "sg_rune.h"

#define SG_MAXBOTS      16
#define SG_DOOR_APPROACH_MAX_MASTERS 16

/* One direct-trigger approach is a persistent fixed-step transaction.  Its
 * one-command callback ticket is separate so pause/retain can clear only the
 * ephemeral authority without erasing continuous-air or finalization time. */
typedef struct sg_door_approach_identity_s
{
	unsigned long long bot_instance;
	int bot_slot;
	int link;
	int from;
	int to;
	int action;
	int controller_kind;
	uint32_t entry_key;
	uint32_t mover_key;
	const rune_t *rune;
	rune_artifact_t rune_artifact;
	sg_mover_owner_t guard_owner;
	sg_mover_ticket_t guard_ticket;
	qboolean active;
} sg_door_approach_identity_t;

typedef struct sg_door_approach_ticket_s
{
	pmove_state_t expected_pms;
	short expected_mins_q8[3];
	short expected_maxs_q8[3];
	int expected_watertype;
	int expected_waterlevel;
	uint32_t support_key;
	uint32_t support_generation;
	unsigned long long bot_instance;
	int bot_slot;
	int link;
	int from;
	int to;
	int action;
	int controller_kind;
	int frame;
	int substep;
	uint32_t entry_key;
	uint32_t mover_key;
	const rune_t *rune;
	rune_artifact_t rune_artifact;
	sg_mover_owner_t guard_owner;
	sg_mover_ticket_t guard_ticket;
	sg_door_approach_state_t pre_state;
	sg_door_approach_state_t predicted_state;
	sg_door_approach_state_t sealed_predicted_state;
	qboolean grounded;
	qboolean expected_touch;
	qboolean touch_seen;
	qboolean activation_seen;
	uint32_t activation_master_keys[SG_DOOR_APPROACH_MAX_MASTERS];
	uint32_t activation_master_seen_mask;
	uint8_t activation_master_count;
	qboolean armed;
} sg_door_approach_ticket_t;

typedef struct sg_bot_s
{
	edict_t		*ent;
	qboolean	active;
	/* Process-lifetime identity for one active occupant of this SG slot.
	 * Assigned once after its first ClientBegin and erased only by reset. */
	unsigned long long instance_token;
	/* Shared mover ownership survives death/respawn and is retired before the
	 * containing process-storage slot is erased. */
	sg_compound_guard_bot_t compound_guard;
	int			seed;           /* seed we believe we are at/near */
	float		stuck_time;     /* accumulated time without progress */
	vec3_t		stuck_origin;   /* dedicated short-range progress sample;
	                             * last_origin belongs to seed localization */
	float		next_report;
	float		next_cmdlog;
	vec3_t		last_origin;
	qboolean	seedless_active; /* bounded recovery after topology is lost */
	float		seedless_since;
	float		seedless_turn_until;
	float		seedless_yaw;

	/* hook execution, two-phase: aim this frame (ClientThink turns the cmd
	 * angles into v_angle), fire immediately after, since Weapon_Hook_Fire
	 * launches along v_angle; then release before the p_weapon.c brake band */
	unsigned	dither_salt;    /* route dither: deterministically mixed per visit
	                             * so a choice holds within a visit but
	                             * varies without consuming the global RNG */
	vec3_t		hp_cur_dep;     /* hook ping-pong: this ride's departure */
	vec3_t		hp_prev_dep;    /* previous ride's departure point */
	float		hp_prev_land;   /* when the previous ride completed */
	float		linger_since;   /* anti-linger: when this non-escort first
	                             * came within 400u of its own carrier;
	                             * 0 = not currently adjacent */
	qboolean	linger_hot;     /* detection latched this think; consumed
	                             * by the de-pace throttle at cmd time */
	int			hook_phase;     /* 0 none, 1 aimed+firing, 2 rope out,
	                             * 3 released mid-air, steering to land */
	int			hook_link;      /* which link this ride is executing */
	/* Diagnostic-only paired record. It never participates in hook control. */
	sg_hook_diagnostic_state_t hook_diagnostics;
	qboolean	hook_bite_logged;   /* one HOOKBITE line per ride */
	qboolean	hook_attached_validated; /* static bite/source checked once */
	float		hook_landbrake; /* stand the landing like the proof did:
	                             * the phantom ARRIVED at a stop; a body
	                             * at 343 skids off the narrow step and
	                             * falls back into the basin it climbed
	                             * out of (iter-19 lmctf03, Gate) */
	vec3_t		hook_anchor;
	vec3_t		hook_view;      /* quantized fire view held for the proved ride;
	                             * production pull starts at this view's muzzle */
	vec3_t		hook_source;    /* exact fixed-point proof source; graph hooks fire
	                             * only when the live body still matches it */
	pmove_state_t	hook_source_pms; /* post-aim state the online witness cloned */
	pmove_state_t	hook_attach_pms; /* expected state when the bolt becomes taut */
	qboolean	hook_source_water; /* water witness permits proved outbound drift */
	int			hook_source_health; /* damage invalidates the cloned launch witness */
	qboolean	hook_attach_groundentity;
	int			hook_attach_watertype;
	int			hook_attach_waterlevel;
	vec3_t		hook_dest;      /* the link's destination seed origin */
	float		hook_deadline;
	int			hook_pull_ms;   /* exact 25 ms attached traversal clock */
	int			hook_settle_ms; /* literal 25 ms post-release proof clock */
	int			hook_proved_pull_ms;   /* online witness boundary, not level.time */
	int			hook_proved_release_ms;
	int			hook_proved_arrival_ms;
	int			hook_proved_settle_ms;

	/* Ordinary RL_HOOK is reducer-owned from successful fire through the
	 * terminal settlement frame.  Keep the live bolt identity separately from
	 * the graph link: a recycled or externally-aborted hook must not inherit
	 * this witness. */
	sg_hook_replay_state_t hook_replay;
	qboolean	hook_replay_active;
	int		hook_replay_link;
	/* Adapter-approved command held across all later host writers until the
	 * final comparison immediately before ClientThink. */
	sg_hook_live_command_guard_t hook_final_guard;
	edict_t		*hook_entity;
	/* Host-owned command history for the independent legacy differential.
	 * It deliberately does not mirror reducer phase: release completes the
	 * current fixed-view 100 ms frame before this switches to settlement. */
	qboolean	hook_legacy_settle;
	qboolean	hook_legacy_arrived;

	/* a link chosen for seconds while the bot goes nowhere is a link the
	 * body cannot execute, whatever the rune thinks -- shelve it awhile.
	 * 32 slots: one doorway feeds 25 links from a single seed (662), and
	 * an 8-slot shelf recycled them faster than they expired. */
#define SG_BL_MAX 32
	int			bl_link[SG_BL_MAX];
	float		bl_until[SG_BL_MAX];

	/* a door that would not yield from this side: a wall, for a while.
	 * bd2's triggers are all SOUTH of it -- approached from the north it
	 * simply does not open, and links proven with doors held open are
	 * runtime lies from that side (Trace: 396 of 416 seconds pinned). */
#define SG_DEAD_DOORS 4
	edict_t		*dead_door[SG_DEAD_DOORS];
	float		dead_door_until[SG_DEAD_DOORS];
	edict_t		*door_hold_ent;     /* the door currently being waited on */
	int			door_hold_link;    /* attempt identity; a lost feeler must not
	                                 * restart the bounded wait */
	float		door_hold_deadline;
	qboolean	deaddoor_ahead;     /* last frame's goal line hit a door
	                                 * already known dead: shelve fast */
	vec3_t		deaddoor_spot;      /* where that dead door was struck */
	qboolean	door_held_last;     /* stood still for a door last frame:
	                                 * commanded stillness, not link failure */
	qboolean	mate_block_last;    /* a TEAMMATE was the obstruction: not
	                                 * the link's failure either */
	qboolean	def_stand;          /* this defender is the stand statue;
	                                 * false = the patrol, which never pins */
	/* A posted defender's short live-combat orbit. The target's slot plus
	 * current-life CTF identity prevent a reconnect from inheriting a turn;
	 * expiry keeps this a local engagement preference, not navigation state. */
	int			defcombat_enemy_slot;
	unsigned long	defcombat_enemy_ctfid;
	int			defcombat_tangent_sign;
	float			defcombat_tangent_until;
	qboolean	was_carrying;       /* for the carry-duration bookend */
	float		carry_start;
	int			last_role;          /* role-transition observability */
	qboolean	death_taught;       /* one danger lesson per death */

	/* loop detection wider than the watch's 96-unit ball: recent seeds
	 * visited with the goal value each visit held. Coming back no better
	 * is an orbit whatever its diameter (a carrier hook-cycled a 250-unit
	 * triangle for minutes; the ball never saw it) */
#define SG_VISIT_RING 8
	int			visit_seed[SG_VISIT_RING];
	int			visit_goal[SG_VISIT_RING];  /* the seed's value at visit */
	int			visit_min[SG_VISIT_RING];   /* best goal reached SINCE */
	float		visit_time[SG_VISIT_RING];
	int			visit_head;
	int			orbit_last_seed; /* last seed consumed by the wide-orbit edge;
		                             * belongs to this bot/life, not the process */

	/* rocket-jump execution: the proof stored the aim (anchor[0/1], z
	 * recoverable) and the worst-case health price (anchor[2]); the body
	 * pays it only with the launcher up and the margin in hand */
	int			rj_phase;           /* 0 none, 1 raising RL, 2 aim+fire,
	                                 * 3 flying the arc */
	vec3_t		rj_aim;             /* unit vector the proof fired on */
	vec3_t		rj_dest;
	float		rj_deadline;
	float		rj_fire_until;      /* how long phase 2 holds the trigger */
	float		rj_use_next;        /* weapon-switch request rate limit */
	int			watch_link;     /* the link under progress-watch */
	float		watch_since;
	vec3_t		watch_org;
	int			jump_link;      /* one-hop RL_JUMP controller owner */
	qboolean	jump_started;   /* launch tap was submitted; never re-tap */
	int			drop_link;      /* RL_DROP controller owner, -1 = unlatched */
	qboolean	drop_started;   /* exact source/rest gate passed */
	qboolean	drop_walkoff;   /* crossed the <=8u lip handoff this action */
	qboolean	drop_airborne;  /* at least one proved 25 ms step lost support */
	qboolean	drop_recover;   /* first proved landing made; walk to exact `to` */
	sg_drop_replay_state_t drop_replay; /* ordinary RL_DROP only */
	qboolean	drop_replay_active;
	int			drop_replay_link; /* identity guard, -1 outside reducer ownership */
	sg_drop_live_events_t drop_live_events; /* host events since last 25 ms read */
	vec3_t		drop_live_step_origin; /* real origin before pending ClientThink */
	qboolean	swim_validated; /* online witness from actual fixed-point entry */
	int			swim_proved_ms; /* exact shared 100 ms arrival boundary */
	int			swim_elapsed_ms;
	sg_swim_replay_state_t swim_replay; /* ordinary RL_SWIM reducer only */
	qboolean	swim_replay_active;
	int			swim_replay_link; /* identity guard, -1 outside reducer ownership */
	int			swim_air_seed;  /* last submerged graph state for breath escape */
	qboolean	declared_activated; /* lift reached TOP / teleporter fired;
	                                 * controller now owns the egress to `to` */
	qboolean	declared_started; /* exact graph source was reached before the
	                              * mechanism approach took command */
	sg_door_approach_state_t declared_door_approach;
	sg_door_approach_identity_t declared_door_approach_identity;
	sg_door_approach_ticket_t declared_door_ticket;
	int			declared_start_frame; /* RL_DOOR starts its proved approach on
		                                      * the next outer-frame boundary */
	qboolean	declared_touched; /* expected RL_DOOR Touch_Multi accepted the
		                           * live player contact, even while cooling */
	int			declared_touch_frame; /* first-contact outer frame; remaining
		                                  * 25 ms commands reproduce the proof's pause */
	qboolean	declared_button_latched; /* exact first BUTTON_DOOR touch sealed
		                                  * its support mode and endpoints */
	qboolean	declared_button_rider; /* entry brush, rather than immutable
		                              * world, was the actual Pmove support */
	short		declared_button_start_q8[3];
	short		declared_button_end_q8[3];
	qboolean	declared_triggered; /* this RL_DOOR's validated Touch_Multi
		                                * actually fired for this bot */
	int			declared_trigger_frame; /* outer frame of that synchronous touch;
		                                       * remaining 25 ms commands stop */
	int			declared_egress_proof_frame; /* cap live TOP rollout to once per
		                                           * outer frame */
	qboolean	declared_door_retreat; /* live suffix to `to` failed; own a
		                                  * separately proved return to anchor */
	int			declared_door_suffix_ms; /* current live forward/retreat proof */
	qboolean	declared_guard_paused; /* RL_DOOR is locally held while proof-law
		                                  * authority is absent; the durable guard
		                                  * may be ACTIVE or PAUSED */
	float		declared_guard_pause_started; /* wall time excluded from the
		                                           * retained action deadline */
	float		declared_door_recovery_since; /* compatible-law physical failure
		                                           * budget; zero while making progress */
	int			commit_link;    /* the gradient step being held */
	float		commit_until;
	/* Exact route transaction selected while the strike coordinator owns its
	 * bounded weapon diversion.  The immutable core deadline is copied here;
	 * draining means authority ended after a physical controller began. */
	int			strike_weapon_link;
	float		strike_weapon_until;
	qboolean	strike_weapon_draining;
	vec3_t		stag_org;       /* stagnation ball on the BODY, not the
	                             * link: the identity watch above resets
	                             * whenever the argmin flaps, and two
	                             * near-equal links flapping at the commit
	                             * period parked Fiend on one drop lip for
	                             * a full lmctf01 match (iter 41) */
	float		stag_since;
	float		stag_next;      /* escalation: one shelve per 2s while parked */
	vec3_t		wedge_org;      /* the unstick of last resort watches from here */
	float		wedge_since;
	qboolean	nav_drove;      /* last frame, navigation drove the legs */
	qboolean	engaged_last;   /* last frame, combat owned the fight */
	int			fan_side;       /* latched detour side: -1 left, +1 right */
	float		fan_side_until;
	float		escape_until;   /* backing out of a concave pocket */
	float		escape_yaw;
	uint32_t	escape_random;  /* private pocket-retreat sequence */
	int			last_goalcost;  /* this frame's goal-field cost, for mates */
	float		vy_cur, vp_cur; /* the view's ACTUAL heading: slew state */
	qboolean	view_on;        /* slew state valid (false snaps on respawn) */
	int			nade_phase;     /* 0 idle, 1 switching, 2 cooking */
	float		nade_until;
	float		nade_next;      /* throw cadence */
	vec3_t		nade_at;        /* where the bomb is going */
	/* Exact live client armed by the flag-room pre-breach grenade.  The
	 * switch/cook deadlines distinguish this transaction from other grenade
	 * uses sharing nade_phase. */
	int			nade_target_slot;       /* g_edicts index, 0 means no bound target */
	unsigned long	nade_target_ctfid;      /* current-life CTF identity for that slot */
	float		nade_target_switch_until;
	float		nade_target_cook_until;
	int			hookfail_streak; /* consecutive failed rides */
	float		hookban_until;  /* streak of 2: the rope is confiscated */
	qboolean	flow_release;   /* cut early on momentum: no landing brake */
	qboolean	speedhook;      /* this rope is a burst, not a transit */
	float		speedhook_next; /* cooldown on burst ropes */
	int			carry_startcost; /* field cost at the grab: breakout gauge */
	int			carry_bestcost;  /* least field cost this carry has reached */
	float		carry_lost_at;   /* last progress-loss event: rate limiter */
	float		rally_since;    /* waiting for a partner before the push */
	int			sticky_link;    /* incumbent route link: challengers must
	                             * beat it by the switching margin */
	int			legs;           /* objective-leg counter: the jitter seed
	                             * re-rolls when the GOAL changes, not
	                             * only on death. Without this, ten bots
	                             * rode one tilt each for 15 minutes and
	                             * stacked back into ropes. */
	int			last_role_for_legs;
	int			lives;          /* respawn count: the route-jitter seed
	                             * (a LIFE rides one opinion of the map) */
	int			was_dead;
	float		ribbon_off;
	float		ribbon_goal;    /* the offset DRIFTS toward a goal
	                             * resampled every ~1.5s -- one run
	                             * sweeps the band instead of riding a
	                             * lane (the railroad-artifact verdict) */
	float		ribbon_next;     /* per-leg lateral offset (sg_ribbon):
	                             * sampled once per committed link so
	                             * repeated runs spread into a band --
	                             * the film judge's rope-vs-brush tell */
	int			ribbon_link;
	uint32_t	ribbon_random;  /* private lane/drift sequence; never global RNG */
	float		latch_until;    /* link latch: the incumbent holds its
	                             * seat until this time unless beaten by
	                             * 15% -- re-decision cadence matched to
	                             * the surface's 1Hz refresh, not the
	                             * 10Hz physics tick (sg_linklatch) */
	/*
	 * THE EARLY-RETURN ERRAND (sg_itemlead). The pad this bot left for ahead
	 * of the team's clock, and the clock it left on. lead_ent 0 is "no errand"
	 * -- entity 0 is the world and can never be a pad -- so the whole feature
	 * is off for a bot until something sets it.
	 */
	int			lead_ent;       /* edict index of the pad, 0 = no errand */
	int			lead_slot;      /* its row in sg_caco_items */
	int			lead_seed;      /* the pad's seed: where the errand goes */
	float		lead_at;        /* T, this team's believed return time */
	float		lead_since;     /* when this errand committed (miscall cap) */
	int			lead_state;     /* waiting short of pad, then spawned pickup */
	float		lead_seen_up_at;/* sight watermark when the errand committed */
	float		lead_inferred_until; /* bounded clock-only spawn pursuit */
	uint32_t	lead_random;    /* private early-item timing sequence */
	int			patrol_seed;    /* current patrol leg target, -1 = holding */
	int			patrol_link;    /* exact direct RUN authority for that target */
	float		patrol_until;   /* when the next leg may start */
	uint32_t	patrol_random;  /* private route/dwell sequence; never global RNG */
	int			def_shift_seed; /* threat-responsive guarded RUN target */
	int			def_shift_link; /* direct RUN authority for that target */
	int			def_shift_from; /* do not bounce straight back here */
	float		def_shift_until;/* bounded leg deadline */
	float		def_shift_next; /* cadence between guarded adjustments */
	/* rank-zero watchman supply sortie. The item field is the authoritative
	 * route bound; these fields retain only the explicit transaction/deadline,
	 * so a role, flag, life, or map edge cannot inherit an errand. */
	qboolean	def_supply_armed;
	int			def_supply_phase;  /* OUTBOUND weapon / RETURN own stand */
	unsigned long long def_supply_instance; /* client-life identity guard */
	int			def_supply_ent;   /* selected live weapon edict, not a class sig */
	int			def_supply_target_seed;
	int			def_supply_route_ms;
	vec3_t		def_supply_target_org;
	float		def_supply_until; /* hard return deadline */
	float		def_supply_next;  /* cancellation backoff before re-arming */
	float		lead_next;      /* attempt cadence while nothing is claimed */

	/*
	 * THE MEGA OFFER (sg_megaworth), for the debug line only -- the pricing
	 * itself is stateless. mega_on is last frame's offer, so the commit prints
	 * on the edge; mega_hp is last frame's health, so a +100 jump is a take.
	 * mega_hp 0 means "no reading" and is what a corpse leaves behind, which
	 * is why a respawn from 5 to 100 cannot read as a pickup.
	 */
	qboolean	mega_on;
	int			mega_hp;
	float		mega_since;     /* when the standing offer turned on */
	float		mega_next;      /* offers refused until here (the back-off) */

	float		runetoss_next;  /* rune handoff cadence (sg_runetoss) */
	float		handoff_next;   /* flag handoff cadence (sg_handoff): a pass
	                             * the receiver never picks up leaves the
	                             * flag on the floor for our own attackers
	                             * to re-grab, and the re-grabber arrives at
	                             * the same low health -- without a cooldown
	                             * that is a drop loop */
	float		soundfire_next; /* speculative rocket cadence */
	float		runeconv_until; /* courier window: converge on carrier */
	float		nav_yaw_cur;    /* smoothed walk heading (sg_smooth) */
	float		nav_yaw_t;      /* its clock */
	int			tac_seed;       /* committed tactical waypoint (-1 none) */
	float		tac_time;       /* when the waypoint was committed */
	int			tac_role;       /* role the waypoint serves: strategy
	                             * change retires the tactic */
	float		strict_since;   /* the strict grab-hold's OWN clock -- it
	                             * once shared rally_since long enough for
	                             * the approach-band rally reset it every
	                             * pairing pass: the 20s hold never ran 20s,
	                             * and GRABMODE showed grabs at room=5
	                             * stamped "clean". */
	int			last_room;      /* defenders believed at the stand, last census */
	int			rally_cover;    /* the low-exposure seed the wait happens at */
	int			rail_link;      /* RUN link being retried the proof's way */
	int			rail_stage;     /* 0 off, 1 walk to from-seed, 2 drive line */
	float		rail_until;

	/*
	 * THE RAIL RHYTHM (sg_railrhythm). The lane this bot is waiting out:
	 * who is believed to be holding it, when the wait started, and how
	 * long this bot is willing to let it last. railhold_since is the
	 * sentinel -- 0 is "not waiting", the same way rally_since is -- so
	 * that a slot never zeroed between maps cannot arrive wearing a wait
	 * it did not choose.
	 */
	float		railhold_since;
	float		railhold_patience;
	float		railhold_next;      /* refractory: earliest a NEW wait may be
	                                 * armed. Without it a wait that expires
	                                 * re-arms on the very next frame from
	                                 * the same unchanged geometry -- the bot
	                                 * never moved, so nothing about the two
	                                 * traces is different -- and the cap the
	                                 * whole design rests on is not a cap at
	                                 * all, it is a stutter that lasts as
	                                 * long as the sighting does */
	int			railhold_enemy;     /* client number, for the log line */

	/* exit-lane asymmetry (sg_exitasym): the links ridden in on this leg,
	 * a ring so the last 16 survive any errand */
	int			inlinks[16];
	int			inlinks_n;
	int			exitasym_set[16];   /* the inbound set, snapshotted at the grab */
	int			exitasym_n;
	qboolean	exitasym_armed;     /* this carry's coin, rolled once at the grab */

	/* human escape priors (sg_escapeprior): the exit this carry drew from
	 * the corpus, the stand it is measured from, and how long the draw
	 * still steers. -1 = this carry drew nothing and prices as before. */
	int			escprior_bucket;
	float		escprior_until;
	float		escprior_dose;      /* the discount, folded at the draw */
	vec3_t		escprior_org;       /* the robbed stand's seed origin */
	int			fake_ping;          /* synthetic ping base, rolled at join (owner: 5-15, near-local) */
	int			prev_seed;          /* the seed most recently LEFT (sg_nobacktrack) */
	float		prev_seed_time;
	float		term_brake;         /* terminal cornering: throttle vs alignment */
	qboolean	terminal;           /* inside the last-ten-meters gate this frame */
	qboolean	sink_ban;           /* wet carrier: downward steps out of contention */
	float		breather_until;     /* sg_breather: sub-max throttle window */
	float		breather_next;      /* next roll of the breather dice */
	/* sg_spawnbeat: the orientation beat a player takes on respawn */
	float		beat_from;          /* when the beat started */
	float		beat_until;         /* and when it gives the legs back */
	float		beat_arc;           /* half-width of the look sweep, degrees */
	int			beat_sign;          /* which shoulder it checks first */
	qboolean	beat_ready;         /* this bot has run dead frames on THIS
		                                 * level; the first spawn must not wear a
		                                 * beat it never earned */
	float		plan_next;          /* sg_drawplan: next in-world plan draw */

	/*
	 * sg_airstrafe: the co-rotating air-strafe chain. One clock (the lean
	 * sinusoid), one chain record (when it started, what it entered at,
	 * what it peaked at), one sampler for the report.
	 */
	float		as_phase;           /* radians; the lean sinusoid's clock */
	float		as_since;           /* chain start, 0 = no chain running */
	float		as_entry;           /* 2D speed the chain was entered at */
	float		as_peak;            /* fastest 2D speed seen inside it */
	unsigned	as_said;            /* 1-in-8 sampler for the chain line */
	sg_human_speed_timer_t as_landing; /* exact 8 ms landing-lock cadence */
	qboolean	as_landing_command; /* next ClientThink belongs to the chain */
	qboolean	as_landing_pending; /* owned ClientThink awaiting real Pmove */
	byte		as_landing_flags_before;
	sg_human_speed_step_t as_landing_before;

	/*
	 * DEATH LANE MEMORY (sg_tilt). Where the last life ended, who ended
	 * it, and the seeds within two links of the spot -- the lane this bot
	 * will not walk back into for a while. Per bot, never shared: tilt is
	 * a grudge, and a grudge belongs to whoever earned it.
	 */
#define SG_TILT_LANE	64          /* seeds kept per lane; see Tilt_Lane */
	int			tilt_lane[SG_TILT_LANE];
	int			tilt_lane_n;
	int			tilt_seed;          /* the seed the last death happened at */
	int			tilt_killer_seed;   /* where the killer was BELIEVED, -1 none */
	float		tilt_death_time;    /* when it happened: the repeat clock */
	float		tilt_window;        /* how long the next life avoids the lane */
	float		tilt_until;         /* armed at respawn, expires on its own */
	float		tilt_caution_until; /* post-death willingness damping */
	unsigned	tilt_said;          /* 1-in-16 sampler for the price line */
} sg_bot_t;

extern sg_bot_t sg_bots[SG_MAXBOTS];

/*
 * The frame context (2026-08-12 standards pass): everything one think
 * frame decides, built once in SG_BotThink and handed to every stage.
 * Stages unpack what they read at their top and write back what they
 * emit -- one parameter instead of thirty, and a new stage costs a
 * field, not eleven signature edits.
 */
typedef struct sg_think_s {
	edict_t			*e;
	usercmd_t		cmd;
	const int		*goal_field, *support, *intercept;
	const int		*route_field;
	qboolean		route_pure;
	const sg_weights_t *w;
	sg_weights_t	live;
	sg_role_t		role;
	int				team, bestlink;
	float			bestval, incumbent_v;
	/* the pricing terms, resolved once per frame where the link fan is
	 * priced; every consumer reads them from the context */
	const int		*danger;
	qboolean		push;
	int				health;
	float			mega;
	qboolean		carrying;
	vec3_t			move_dir;
	float			view_yaw, view_pitch;
	qboolean		have_move, open_ahead, run_link, precision;
	qboolean		hold_post, rally_hold, rail_hold, patrol_walk, think_over;
	int				rail_seed, rail_client;
	float			rail_dose, post_yaw, post_sight;
	int				door_hold;
	edict_t			*door_ent;
	qboolean		drop_yaw_locked;
	double			drop_yaw; /* preserve DROP's double-M_PI command atom */
	qboolean		drop_source_door_pending; /* first 25 ms observation only */
	qboolean		hook_brake;
	qboolean		jump_launch;    /* this frame owns the one proved launch tap */
	qboolean		duel, duel_hold;
	vec3_t			duel_org;
	float			duel_want, duel_expo;
	/* Frame-scoped strike policy.  The coordinator owns membership and
	 * deadlines; the ordinary descent/move stages own route execution. */
	qboolean		strike_active, strike_hold, strike_rush,
				strike_pressure,
				scoop_mission,
				combat_pursuit,
				rearguard,
				escort_mission,
				escort_formation,
				strike_blocks_optional,
				strike_weapon_pursuit;
	float			strike_weapon_deadline;
} sg_think_t;

void SG_BotThink(sg_bot_t *bot);
void SG_StrikeSlotReset(int slot);
qboolean SG_StrikeEnemyPressureSnapshot(const sg_bot_t *bot);
int SG_StrikeEnemyPressureGoalSnapshot(const sg_bot_t *bot);
qboolean SG_LevelSetup(void);
void Botfill_Frame(void);

#endif
