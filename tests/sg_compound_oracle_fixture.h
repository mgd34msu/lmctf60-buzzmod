#ifndef SG_COMPOUND_ORACLE_FIXTURE_H
#define SG_COMPOUND_ORACLE_FIXTURE_H

#include <math.h>
#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_compound.h"
#include "slipgate/sg_compound_world.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_util.h"
#include "sg_compound_hook_oracle_fixture.h"

typedef enum fixture_suffix_e
{
	FIXTURE_SUFFIX_SUCCESS = 0,
	FIXTURE_SUFFIX_NO_SWEEP,
	FIXTURE_SUFFIX_REENTRY,
	FIXTURE_SUFFIX_ARRIVE_BEFORE_CLEAR,
	FIXTURE_SUFFIX_ALWAYS_OUTSIDE,
	FIXTURE_SUFFIX_BETWEEN_RECROSS,
	FIXTURE_SUFFIX_PRECLEAR_CHORD,
	FIXTURE_SUFFIX_POSTCLEAR_CHORD
} fixture_suffix_t;

typedef struct fixture_config_s
{
	int touch_substep;
	fixture_suffix_t suffix;
	qboolean contaminate_trigger;
	qboolean contaminate_solid;
	qboolean hazard_ride;
	qboolean fall_ride;
	qboolean wrong_contact;
	qboolean unstable_contact;
	qboolean opening_drift;
	qboolean source_hazard;
	qboolean source_dry;
	qboolean force_foreign_trigger;
	qboolean suffix_hazard;
	qboolean drop_suffix;
	qboolean hook_suffix;
	qboolean hook_discover_control;
	qboolean hook_muzzle_blocked;
	qboolean hook_shot_sky;
	qboolean hook_shot_nonworld;
	qboolean hook_bolt_trigger;
	qboolean suffix_fall;
	qboolean suffix_foreign_trigger;
	qboolean suffix_foreign_solid;
	qboolean suffix_nonfinite;
	int hook_sweep_mode;
	qboolean loader_transient;
	qboolean loader_malformed;
	qboolean loader_unowned;
	int top_drift_at_command;
	int identity_drift_at_command;
	float mechanism_x;
	float source_x;
	vec3_t hook_bite;
} fixture_config_t;

typedef struct fixture_observation_s
{
	int pmove_calls;
	int approach_commands;
	int zero_commands;
	int ride_zero_commands;
	int suffix_commands;
	int trace_calls;
	int link_calls;
	float link_origins[16];
	qboolean stage_started;
	qboolean top_staged;
	qboolean first_snapinitial;
	int later_snapinitial;
	qboolean first_top_seen;
	usercmd_t first_top_command;
	int callback_calls;
	int last_pmove_mask;
	int stripped_pmove_masks;
	int normal_pmove_masks;
	int pretop_contact_traces;
	int transient_masked_shot_traces;
	int transient_masked_contact_traces;
} fixture_observation_t;

#define FIXTURE_EDICTS 16
#define GUARD_MASTER_KEY 10
#define GUARD_MEMBER_KEY 11
#define GUARD_TRIGGER_KEY 12
#define GUARD_SOURCE_KEY 13
#define GUARD_EXTRA_KEY 14


extern edict_t fixture_edicts[FIXTURE_EDICTS];
extern gclient_t fixture_clients[5];
extern cvar_t fixture_gravity;
extern fixture_config_t fixture_config;
extern fixture_observation_t fixture_observation;
extern int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

void Set3(vec3_t value, float x, float y, float z);
qboolean CommandZero(const usercmd_t *command);
void door_hit_top(edict_t *self);
void door_hit_bottom(edict_t *self);
void door_go_down(edict_t *self);
void door_use(edict_t *self, edict_t *other, edict_t *activator);
void door_blocked(edict_t *self, edict_t *other);
void Touch_DoorTrigger(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surface);
void Touch_Multi(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surface);
void trigger_relay_use(edict_t *self, edict_t *other,
	edict_t *activator);
void Use_Target_Speaker(edict_t *self, edict_t *other,
	edict_t *activator);
int SG_RuneTestDoorCooldownGapMs(edict_t *trigger);
trace_t HostTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int mask);
int HostPointContents(const vec3_t point);
int HostBoxEdicts(const vec3_t mins, const vec3_t maxs,
	edict_t **list, int max_count, int area_type);
void HostPmove(pmove_t *pmove);
void HostLinkEntity(edict_t *entity);
void PublishDoorCompletion(edict_t *door, sg_mover_completion_kind_t kind);
int SuffixX(void);
void Door(edict_t *door);
void Trigger(edict_t *trigger, edict_t *door, float mechanism_x);
edict_t *GuardDoor(int key);
fixture_config_t DefaultConfig(int touch, fixture_suffix_t suffix);
void ResetFixture(const fixture_config_t *config);
void ResetGuardFixture(void);
void GuardDoorPair(edict_t **master_out, edict_t **member_out);
void InitPhantom(sg_phantom_t *phantom, qboolean damaging_fall);
void SyncRecoveryPassent(const sg_phantom_t *phantom, edict_t *passent);
edict_t *InitRecoveryState(sg_phantom_t *phantom,
	const sg_compound_world_preopen_t *resolved, int suffix_commands);
rune_reject_reason_t Resolve(sg_compound_world_preopen_t *resolved);
qboolean CanonicalHint(sg_compound_world_preopen_t *resolved, vec3_t hint);
qboolean MemberRestored(const edict_t *member, const edict_t *before);
void CheckStaticContextRestored(void);
int SG_CompoundSwimPreopenCasesRun(void);
int SG_CompoundSwimRecoveryCasesRun(void);
int SG_CompoundDeclaredOracleCasesRun(void);

#endif
