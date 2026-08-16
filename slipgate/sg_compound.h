/* sg_compound.h -- pure outer transaction law for compound RUNE actions. */
#ifndef SG_COMPOUND_H
#define SG_COMPOUND_H

#include <stdint.h>

#include "sg_rune_wire.h"

/* Compound links remain unpublishable until joint mover replay and the live
 * outer controller reach this revision.  This gate is deliberately separate
 * from generated action metadata so a registry-only change cannot authorize
 * structurally valid but unreplayed compound records. */
#define SG_COMPOUND_REQUIRED_CONTROLLER_REVISION 1
#define SG_COMPOUND_LIVE_CONTROLLER_REVISION 0

typedef enum sg_compound_phase_e
{
	SG_COMPOUND_NONE = 0,
	SG_COMPOUND_SOURCE,
	SG_COMPOUND_APPROACH,
	SG_COMPOUND_TOUCHED,
	SG_COMPOUND_OPENING,
	SG_COMPOUND_RIDE,
	SG_COMPOUND_TOP,
	SG_COMPOUND_SUFFIX_LEASED,
	SG_COMPOUND_SUFFIX_CLEAR,
	SG_COMPOUND_RECOVER
} sg_compound_phase_t;

typedef enum sg_compound_event_e
{
	SG_COMPOUND_EVENT_APPROACH = 0,
	SG_COMPOUND_EVENT_TOUCH,
	SG_COMPOUND_EVENT_ACTIVATE,
	SG_COMPOUND_EVENT_RIDE,
	SG_COMPOUND_EVENT_TOP,
	SG_COMPOUND_EVENT_SUFFIX_BEGIN,
	SG_COMPOUND_EVENT_SWEEP_CLEAR,
	SG_COMPOUND_EVENT_ARRIVED,
	SG_COMPOUND_EVENT_ABORT,
	SG_COMPOUND_EVENT_RECOVERED
} sg_compound_event_t;

typedef struct sg_compound_state_s
{
	sg_compound_phase_t phase;
	int link_index;
	int mover_key;
	uint8_t action;
	uint8_t mode;
	uint8_t suffix_action;
	uint8_t sweep_clear;
	uint8_t arrived;
} sg_compound_state_t;

/* Pure replay of the constant-speed Move_Calc/Move_Begin path.  Activating a
 * mover from another entity schedules Move_Begin for the next 100 ms server
 * boundary, so SCHEDULED deliberately emits one zero-displacement frame.
 * Every later displacement is quantized exactly like SV_Push. */
typedef enum sg_compound_translate_phase_e
{
	SG_COMPOUND_TRANSLATE_NONE = 0,
	SG_COMPOUND_TRANSLATE_SCHEDULED,
	SG_COMPOUND_TRANSLATE_FULL,
	SG_COMPOUND_TRANSLATE_FINAL,
	SG_COMPOUND_TRANSLATE_TOP
} sg_compound_translate_phase_t;

typedef struct sg_compound_translate_s
{
	sg_compound_translate_phase_t phase;
	float start[3];
	float end[3];
	float origin[3];
	float direction[3];
	float speed;
	float remaining_distance;
	int full_frames_remaining;
	int elapsed_ms;
} sg_compound_translate_t;

typedef struct sg_compound_translate_step_s
{
	float delta[3];
	float origin[3];
	int elapsed_ms;
	int at_top;
} sg_compound_translate_step_t;

/* The outer controller remains the sole authority.  The callback receives
 * only the already-selected suffix and cannot choose or release the outer
 * action.  A false result leaves the transaction at TOP for bounded recovery
 * or a later authoritative retry. */
typedef int (*sg_compound_suffix_begin_fn)(void *context, int link_index,
	int suffix_action);

int SG_CompoundAction(int action);
int SG_CompoundRuntimeReady(int action);
int SG_CompoundSuffixAction(int action);
void SG_CompoundReset(sg_compound_state_t *state);
int SG_CompoundBegin(sg_compound_state_t *state, int link_index,
	int mover_key, int action, int mode);
int SG_CompoundAdvance(sg_compound_state_t *state,
	sg_compound_event_t event);
int SG_CompoundOwns(const sg_compound_state_t *state, int link_index,
	int mover_key);
int SG_CompoundLeaseHeld(const sg_compound_state_t *state);
int SG_CompoundDelegateSuffix(sg_compound_state_t *state, int link_index,
	int mover_key, sg_compound_suffix_begin_fn begin, void *context);

int SG_CompoundTranslateBegin(sg_compound_translate_t *state,
	const float start[3], const float end[3], float speed);
int SG_CompoundTranslateFrame(sg_compound_translate_t *state,
	sg_compound_translate_step_t *step);

/* Structural/controller validation shared by the native writer and loader.
 * Live map-mechanism replay remains a separate publication gate. */
rune_reject_reason_t SG_CompoundValidateLink(
	const sg_rune_v3_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_v3_link_t *link);

#endif /* SG_COMPOUND_H */
