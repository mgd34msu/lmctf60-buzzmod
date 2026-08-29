#include "sg_host_mechanism_law.h"

#include <math.h>
#include <string.h>

static int SameFloat(float left, float right)
{
	return memcmp(&left, &right, sizeof(left)) == 0;
}

static int WaitMilliseconds(float seconds, uint64_t *milliseconds_out)
{
	long double milliseconds;

	if (!milliseconds_out || !isfinite(seconds) || seconds < 0.0f)
		return 0;
	milliseconds = (long double)seconds * 1000.0L;
	if (milliseconds > (long double)UINT64_MAX)
		return 0;
	*milliseconds_out = (uint64_t)milliseconds;
	return 1;
}

static int AddMilliseconds(uint64_t now_ms, uint64_t delay_ms,
	uint64_t *scheduled_ms)
{
	if (!scheduled_ms || delay_ms > UINT64_MAX - now_ms)
		return 0;
	*scheduled_ms = now_ms + delay_ms;
	return 1;
}

static int AddFrames(uint64_t now_ms, uint64_t frames, uint32_t frame_ms,
	uint64_t *scheduled_ms)
{
	uint64_t delay_ms;

	if (!frame_ms || frames > UINT64_MAX / frame_ms)
		return 0;
	delay_ms = frames * frame_ms;
	return AddMilliseconds(now_ms, delay_ms, scheduled_ms);
}

static void ClearTransition(sg_host_mechanism_transition_t *result)
{
	memset(result, 0, sizeof(*result));
}

static int StateValid(int state)
{
	return state == SG_HOST_MECHANISM_STATE_TOP ||
		state == SG_HOST_MECHANISM_STATE_BOTTOM ||
		state == SG_HOST_MECHANISM_STATE_UP ||
		state == SG_HOST_MECHANISM_STATE_DOWN;
}

static int BlockerKindValid(sg_host_mechanism_blocker_kind_t blocker_kind)
{
	return blocker_kind == SG_HOST_MECHANISM_BLOCKER_NONE ||
		blocker_kind == SG_HOST_MECHANISM_BLOCKER_CLIENT ||
		blocker_kind == SG_HOST_MECHANISM_BLOCKER_MONSTER ||
		blocker_kind == SG_HOST_MECHANISM_BLOCKER_OTHER;
}

static float AccelerationDistance(float target, float rate)
{
	return target * ((target / rate) + 1.0f) / 2.0f;
}

static void CalculateAcceleratedMove(float remaining, float speed,
	float accel, float decel, float *move_speed, float *decel_distance,
	float *current_speed)
{
	float accel_distance;
	float stopping_distance;

	*move_speed = speed;
	if (remaining < accel)
	{
		*current_speed = remaining;
		return;
	}
	accel_distance = AccelerationDistance(speed, accel);
	stopping_distance = AccelerationDistance(speed, decel);
	if ((remaining - accel_distance - stopping_distance) < 0.0f)
	{
		float factor = (accel + decel) / (accel * decel);
		float discriminant = 4.0f - 4.0f * factor *
			(-2.0f * remaining);

		*move_speed = (-2.0f + sqrtf(discriminant)) / (2.0f * factor);
		stopping_distance = AccelerationDistance(*move_speed, decel);
	}
	*decel_distance = stopping_distance;
}

static void AccelerateMove(float remaining, float speed, float accel,
	float decel, float move_speed, float decel_distance,
	float *current_speed, float *next_speed)
{
	if (remaining <= decel_distance)
	{
		if (remaining < decel_distance)
		{
			if (*next_speed != 0.0f)
			{
				*current_speed = *next_speed;
				*next_speed = 0.0f;
				return;
			}
			if (*current_speed > decel)
				*current_speed -= decel;
		}
		return;
	}
	if (SameFloat(*current_speed, move_speed) &&
		(remaining - *current_speed) < decel_distance)
	{
		float first_distance = remaining - decel_distance;
		float second_distance = move_speed *
			(1.0f - (first_distance / move_speed));
		float transition_distance = first_distance + second_distance;

		*current_speed = move_speed;
		*next_speed = move_speed - decel *
			(second_distance / transition_distance);
		return;
	}
	if (*current_speed < speed)
	{
		float old_speed = *current_speed;
		float first_distance;
		float first_speed;
		float second_distance;
		float transition_distance;

		*current_speed += accel;
		if (*current_speed > speed)
			*current_speed = speed;
		if ((remaining - *current_speed) >= decel_distance)
			return;
		first_distance = remaining - decel_distance;
		first_speed = (old_speed + move_speed) / 2.0f;
		second_distance = move_speed *
			(1.0f - (first_distance / first_speed));
		transition_distance = first_distance + second_distance;
		*current_speed = first_speed *
			(first_distance / transition_distance) + move_speed *
			(second_distance / transition_distance);
		*next_speed = move_speed - decel *
			(second_distance / transition_distance);
	}
}

static int ScheduleAccelerated(float distance, float speed, float accel,
	float decel, const sg_host_mechanism_law_t *law,
	sg_host_mechanism_move_result_t *result_out)
{
	float remaining = distance;
	float current_speed = 0.0f;
	float next_speed = 0.0f;
	float move_speed = speed;
	float decel_distance = 0.0f;
	uint64_t callback_ms = law->frame_ms;

	for (;;)
	{
		float prior_remaining = remaining;

		if (!isfinite(remaining) || !isfinite(current_speed) ||
			!isfinite(next_speed) || !isfinite(move_speed) ||
			!isfinite(decel_distance) || current_speed < 0.0f ||
			next_speed < 0.0f || move_speed <= 0.0f ||
			decel_distance < 0.0f)
			return 0;
		remaining -= current_speed;
		if (!isfinite(remaining) || remaining < 0.0f ||
			(current_speed != 0.0f && remaining >= prior_remaining))
			return 0;
		if (current_speed == 0.0f)
			CalculateAcceleratedMove(remaining, speed, accel, decel,
				&move_speed, &decel_distance, &current_speed);
		AccelerateMove(remaining, speed, accel, decel, move_speed,
			decel_distance, &current_speed, &next_speed);
		if (!isfinite(remaining) || !isfinite(current_speed) ||
			!isfinite(next_speed) || !isfinite(move_speed) ||
			!isfinite(decel_distance) || current_speed < 0.0f ||
			next_speed < 0.0f || move_speed <= 0.0f ||
			decel_distance < 0.0f)
			return 0;
		if (remaining <= current_speed)
		{
			result_out->residual_distance = remaining;
			result_out->final_speed = remaining /
				((float)law->frame_ms / 1000.0f);
			if (!isfinite(result_out->final_speed))
				return 0;
			if (remaining == 0.0f)
				result_out->completion_ms = callback_ms;
			else if (!AddMilliseconds(callback_ms, law->frame_ms,
				&result_out->completion_ms))
				return 0;
			return 1;
		}
		if (result_out->full_speed_frames == UINT64_MAX)
			return 0;
		if (SameFloat(current_speed, move_speed))
			result_out->full_speed_frames++;
		if (!AddMilliseconds(callback_ms, law->frame_ms, &callback_ms))
			return 0;
	}
}

void SG_HostMechanismLawDefault(sg_host_mechanism_law_t *law_out)
{
	if (!law_out)
		return;
	memset(law_out, 0, sizeof(*law_out));
	law_out->version = SG_HOST_MECHANISM_LAW_VERSION;
	law_out->frame_ms = 100U;
	law_out->move_equation_id = SG_HOST_MECHANISM_MOVE_EQUATION_ID;
	law_out->acceleration_equation_id = SG_HOST_MECHANISM_ACCEL_EQUATION_ID;
	law_out->door_equation_id = SG_HOST_MECHANISM_DOOR_EQUATION_ID;
	law_out->platform_equation_id = SG_HOST_MECHANISM_PLAT_EQUATION_ID;
	law_out->trigger_equation_id = SG_HOST_MECHANISM_TRIGGER_EQUATION_ID;
	law_out->train_equation_id = SG_HOST_MECHANISM_TRAIN_EQUATION_ID;
	law_out->identity = SG_HOST_MECHANISM_LAW_ID;
	law_out->door_default_wait_ms = 3000U;
	law_out->platform_top_dwell_ms = 3000U;
	law_out->platform_top_touch_delay_ms = 1000U;
	law_out->door_trigger_debounce_ms = 1000U;
	law_out->door_message_debounce_ms = 5000U;
	law_out->train_blocked_debounce_ms = 500U;
	law_out->trigger_default_wait_ms = 200U;
	law_out->trigger_remove_delay_ms = 100U;
	law_out->frame_schedule_ms = 100U;
	/* SP_func_door doubles its stock 100 speed in deathmatch.  The
	 * production publication is the deathmatch host law. */
	law_out->door_default_speed = 200.0f;
	law_out->platform_default_speed = 20.0f;
	law_out->platform_default_accel = 5.0f;
	law_out->platform_default_decel = 5.0f;
	law_out->train_default_speed = 100.0f;
}

int SG_HostMechanismLawValid(const sg_host_mechanism_law_t *law)
{
	if (!law)
		return 0;
	return law->version == SG_HOST_MECHANISM_LAW_VERSION &&
		law->frame_ms == 100U &&
		law->move_equation_id == SG_HOST_MECHANISM_MOVE_EQUATION_ID &&
		law->acceleration_equation_id == SG_HOST_MECHANISM_ACCEL_EQUATION_ID &&
		law->door_equation_id == SG_HOST_MECHANISM_DOOR_EQUATION_ID &&
		law->platform_equation_id == SG_HOST_MECHANISM_PLAT_EQUATION_ID &&
		law->trigger_equation_id == SG_HOST_MECHANISM_TRIGGER_EQUATION_ID &&
		law->train_equation_id == SG_HOST_MECHANISM_TRAIN_EQUATION_ID &&
		law->identity == SG_HOST_MECHANISM_LAW_ID &&
		law->door_default_wait_ms == 3000U &&
		law->platform_top_dwell_ms == 3000U &&
		law->platform_top_touch_delay_ms == 1000U &&
		law->door_trigger_debounce_ms == 1000U &&
		law->door_message_debounce_ms == 5000U &&
		law->train_blocked_debounce_ms == 500U &&
		law->trigger_default_wait_ms == 200U &&
		law->trigger_remove_delay_ms == 100U &&
		law->frame_schedule_ms == 100U &&
		SameFloat(law->door_default_speed, 200.0f) &&
		SameFloat(law->platform_default_speed, 20.0f) &&
		SameFloat(law->platform_default_accel, 5.0f) &&
		SameFloat(law->platform_default_decel, 5.0f) &&
		SameFloat(law->train_default_speed, 100.0f);
}

int SG_HostMechanismMoveSchedule(const sg_host_mechanism_law_t *law,
	float distance, float speed, float accel, float decel, int current_entity,
	sg_host_mechanism_move_result_t *result_out)
{
	float frames;

	if (!result_out)
		return 0;
	memset(result_out, 0, sizeof(*result_out));
	if (!SG_HostMechanismLawValid(law) || !isfinite(distance) ||
		!isfinite(speed) || !isfinite(accel) || !isfinite(decel) ||
		distance < 0.0f || speed <= 0.0f || accel < 0.0f || decel < 0.0f)
		return 0;
	result_out->valid = 1;
	result_out->accelerated = !(SameFloat(speed, accel) &&
		SameFloat(speed, decel));
	if (result_out->accelerated && (accel == 0.0f || decel == 0.0f))
	{
		memset(result_out, 0, sizeof(*result_out));
		return 0;
	}
	/* Move_Calc can enter constant motion synchronously for the current
	 * entity.  Accelerated movers always arm Think_AccelMove on the next
	 * server frame, regardless of current_entity. */
	result_out->first_think_ms = result_out->accelerated || !current_entity ?
		law->frame_ms : 0U;
	if (distance == 0.0f)
	{
		result_out->completion_ms = result_out->first_think_ms;
		return 1;
	}
	if (result_out->accelerated)
	{
		if (!ScheduleAccelerated(distance, speed, accel, decel, law,
			result_out))
		{
			memset(result_out, 0, sizeof(*result_out));
			return 0;
		}
		return 1;
	}
	{
		float frame_distance = speed * ((float)law->frame_ms / 1000.0f);

		if (!isfinite(frame_distance))
		{
			memset(result_out, 0, sizeof(*result_out));
			return 0;
		}
		if (frame_distance >= distance)
		{
			result_out->final_speed = distance /
				((float)law->frame_ms / 1000.0f);
			if (!AddMilliseconds(result_out->first_think_ms, law->frame_ms,
				&result_out->completion_ms))
			{
				memset(result_out, 0, sizeof(*result_out));
				return 0;
			}
			result_out->residual_distance = distance;
			if (!isfinite(result_out->final_speed))
			{
				memset(result_out, 0, sizeof(*result_out));
				return 0;
			}
			return 1;
		}
	}
	{
		float frame_seconds = (float)law->frame_ms / 1000.0f;
		long double frame_count;
		uint64_t full_speed_frames;

		if (!isfinite(frame_seconds) || frame_seconds <= 0.0f)
		{
			memset(result_out, 0, sizeof(*result_out));
			return 0;
		}
		frames = floorf((distance / speed) / frame_seconds);
		if (!isfinite(frames) || frames < 0.0f)
		{
			memset(result_out, 0, sizeof(*result_out));
			return 0;
		}
		frame_count = (long double)frames;
		if (frame_count > (long double)UINT64_MAX)
		{
			memset(result_out, 0, sizeof(*result_out));
			return 0;
		}
		full_speed_frames = (uint64_t)frames;
		result_out->full_speed_frames = full_speed_frames;
		result_out->residual_distance = distance -
			frames * speed * frame_seconds;
		if (!isfinite(result_out->residual_distance) ||
			result_out->residual_distance < 0.0f)
		{
			memset(result_out, 0, sizeof(*result_out));
			return 0;
		}
		result_out->final_speed = result_out->residual_distance /
			frame_seconds;
		if (!isfinite(result_out->final_speed))
		{
			memset(result_out, 0, sizeof(*result_out));
			return 0;
		}
		if (result_out->residual_distance == 0.0f)
		{
			if (!AddFrames(result_out->first_think_ms,
				result_out->full_speed_frames, law->frame_ms,
				&result_out->completion_ms))
			{
				memset(result_out, 0, sizeof(*result_out));
				return 0;
			}
			return 1;
		}
		if (result_out->full_speed_frames == UINT64_MAX)
		{
			memset(result_out, 0, sizeof(*result_out));
			return 0;
		}
		if (!AddFrames(result_out->first_think_ms,
			result_out->full_speed_frames + 1U, law->frame_ms,
			&result_out->completion_ms))
		{
			memset(result_out, 0, sizeof(*result_out));
			return 0;
		}
		return 1;
	}
}

int SG_HostMechanismDoorStepEx(const sg_host_mechanism_law_t *law,
	sg_host_mechanism_door_event_t event, uint32_t flags, int state,
	float wait_seconds, uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_blocker_kind_t blocker_kind, uint32_t damage,
	sg_host_mechanism_transition_t *result_out)
{
	if (!result_out || !SG_HostMechanismLawValid(law) || !StateValid(state) ||
		!BlockerKindValid(blocker_kind))
		return 0;
	if (!isfinite(wait_seconds))
		return 0;
	ClearTransition(result_out);
	result_out->next_state = state;
	if (event == SG_HOST_MECHANISM_DOOR_TOP)
	{
		uint64_t wait_ms;

		result_out->accepted = 1;
		result_out->next_state = SG_HOST_MECHANISM_STATE_TOP;
		if (!(flags & SG_HOST_MECHANISM_DOOR_TOGGLE))
		{
			if (wait_seconds == 0.0f)
				wait_seconds = (float)law->door_default_wait_ms / 1000.0f;
			if (wait_seconds >= 0.0f &&
				(!WaitMilliseconds(wait_seconds, &wait_ms) ||
					!AddMilliseconds(now_ms, wait_ms,
						&result_out->next_think_ms)))
				return 0;
		}
		return 1;
	}
	if (event == SG_HOST_MECHANISM_DOOR_TRIGGER_TOUCH)
	{
		if (now_ms < debounce_until_ms)
			return 1;
		result_out->accepted = 1;
		if (!AddMilliseconds(now_ms, law->door_trigger_debounce_ms,
			&result_out->next_debounce_ms))
			return 0;
		return 1;
	}
	if (event == SG_HOST_MECHANISM_DOOR_MESSAGE_TOUCH)
	{
		if (now_ms < debounce_until_ms)
			return 1;
		result_out->accepted = 1;
		if (!AddMilliseconds(now_ms, law->door_message_debounce_ms,
			&result_out->next_debounce_ms))
			return 0;
		return 1;
	}
	if (event == SG_HOST_MECHANISM_DOOR_BLOCKED)
	{
		if (blocker_kind == SG_HOST_MECHANISM_BLOCKER_NONE)
			return 0;
		result_out->blocker_kind = blocker_kind;
		result_out->accepted = 1;
		if (blocker_kind == SG_HOST_MECHANISM_BLOCKER_OTHER)
		{
			result_out->damaged = 1;
			result_out->destroyed = 1;
			result_out->damage = SG_HOST_MECHANISM_NONCLIENT_DAMAGE;
			return 1;
		}
		if (blocker_kind != SG_HOST_MECHANISM_BLOCKER_CLIENT &&
			blocker_kind != SG_HOST_MECHANISM_BLOCKER_MONSTER)
			return 0;
		result_out->damaged = damage != 0U;
		result_out->damage = damage;
		if (!(flags & SG_HOST_MECHANISM_DOOR_CRUSHER) && wait_seconds >= 0.0f &&
			(state == SG_HOST_MECHANISM_STATE_UP ||
			 state == SG_HOST_MECHANISM_STATE_DOWN))
		{
			result_out->reversed = 1;
			result_out->next_state = state == SG_HOST_MECHANISM_STATE_DOWN ?
				SG_HOST_MECHANISM_STATE_UP : SG_HOST_MECHANISM_STATE_DOWN;
		}
		return 1;
	}
	if (event == SG_HOST_MECHANISM_DOOR_USE)
	{
		result_out->accepted = 1;
		if ((flags & SG_HOST_MECHANISM_DOOR_TOGGLE) &&
			(state == SG_HOST_MECHANISM_STATE_UP ||
			 state == SG_HOST_MECHANISM_STATE_TOP))
		{
			result_out->next_state = SG_HOST_MECHANISM_STATE_DOWN;
			return 1;
		}
		if (state == SG_HOST_MECHANISM_STATE_TOP)
		{
			uint64_t wait_ms;

			result_out->next_state = SG_HOST_MECHANISM_STATE_TOP;
			if (wait_seconds == 0.0f)
				wait_seconds = (float)law->door_default_wait_ms / 1000.0f;
			if (wait_seconds >= 0.0f &&
				(!WaitMilliseconds(wait_seconds, &wait_ms) ||
					!AddMilliseconds(now_ms, wait_ms,
						&result_out->next_think_ms)))
				return 0;
		}
		else if (state == SG_HOST_MECHANISM_STATE_UP)
			result_out->next_state = SG_HOST_MECHANISM_STATE_UP;
		else
			result_out->next_state = SG_HOST_MECHANISM_STATE_UP;
		return 1;
	}
	return 0;
}

int SG_HostMechanismDoorStep(const sg_host_mechanism_law_t *law,
	sg_host_mechanism_door_event_t event, uint32_t flags, int state,
	float wait_seconds, uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_transition_t *result_out)
{
	return SG_HostMechanismDoorStepEx(law, event, flags, state, wait_seconds,
		now_ms, debounce_until_ms, SG_HOST_MECHANISM_BLOCKER_CLIENT,
		SG_HOST_MECHANISM_DEFAULT_DOOR_DAMAGE, result_out);
}

int SG_HostMechanismPlatformStepEx(const sg_host_mechanism_law_t *law,
	sg_host_mechanism_platform_event_t event, int state, uint64_t now_ms,
	uint64_t debounce_until_ms, sg_host_mechanism_blocker_kind_t blocker_kind,
	uint32_t damage, sg_host_mechanism_transition_t *result_out)
{
	if (!result_out || !SG_HostMechanismLawValid(law) || !StateValid(state) ||
		!BlockerKindValid(blocker_kind))
		return 0;
	ClearTransition(result_out);
	result_out->next_state = state;
	if (event == SG_HOST_MECHANISM_PLATFORM_TOP)
	{
		result_out->accepted = 1;
		result_out->next_state = SG_HOST_MECHANISM_STATE_TOP;
		if (!AddMilliseconds(now_ms, law->platform_top_dwell_ms,
			&result_out->next_think_ms))
			return 0;
		return 1;
	}
	if (event == SG_HOST_MECHANISM_PLATFORM_TRIGGER_TOUCH)
	{
		(void)debounce_until_ms;
		result_out->accepted = 1;
		if (state == SG_HOST_MECHANISM_STATE_BOTTOM)
			result_out->next_state = SG_HOST_MECHANISM_STATE_UP;
		else if (state == SG_HOST_MECHANISM_STATE_TOP)
		{
			if (!AddMilliseconds(now_ms, law->platform_top_touch_delay_ms,
				&result_out->next_think_ms))
				return 0;
		}
		return 1;
	}
	if (event == SG_HOST_MECHANISM_PLATFORM_BLOCKED)
	{
		if (blocker_kind == SG_HOST_MECHANISM_BLOCKER_NONE)
			return 0;
		result_out->blocker_kind = blocker_kind;
		result_out->accepted = 1;
		if (blocker_kind == SG_HOST_MECHANISM_BLOCKER_OTHER)
		{
			result_out->damaged = 1;
			result_out->destroyed = 1;
			result_out->damage = SG_HOST_MECHANISM_NONCLIENT_DAMAGE;
			return 1;
		}
		if (blocker_kind != SG_HOST_MECHANISM_BLOCKER_CLIENT &&
			blocker_kind != SG_HOST_MECHANISM_BLOCKER_MONSTER)
			return 0;
		result_out->damaged = damage != 0U;
		result_out->damage = damage;
		if (state == SG_HOST_MECHANISM_STATE_UP)
		{
			result_out->reversed = 1;
			result_out->next_state = SG_HOST_MECHANISM_STATE_DOWN;
		}
		else if (state == SG_HOST_MECHANISM_STATE_DOWN)
		{
			result_out->reversed = 1;
			result_out->next_state = SG_HOST_MECHANISM_STATE_UP;
		}
		return 1;
	}
	return 0;
}

int SG_HostMechanismPlatformStep(const sg_host_mechanism_law_t *law,
	sg_host_mechanism_platform_event_t event, int state, uint64_t now_ms,
	uint64_t debounce_until_ms, sg_host_mechanism_transition_t *result_out)
{
	return SG_HostMechanismPlatformStepEx(law, event, state, now_ms,
		debounce_until_ms, SG_HOST_MECHANISM_BLOCKER_CLIENT,
		SG_HOST_MECHANISM_DEFAULT_DOOR_DAMAGE, result_out);
}

int SG_HostMechanismTriggerStep(const sg_host_mechanism_law_t *law,
	int already_triggered, float wait_seconds, uint64_t now_ms,
	sg_host_mechanism_transition_t *result_out)
{
	if (!result_out || !SG_HostMechanismLawValid(law))
		return 0;
	ClearTransition(result_out);
	if (already_triggered)
		return 1;
	if (!isfinite(wait_seconds))
		return 0;
	result_out->accepted = 1;
	if (wait_seconds == 0.0f)
		wait_seconds = (float)law->trigger_default_wait_ms / 1000.0f;
	if (wait_seconds > 0.0f)
	{
		uint64_t wait_ms;

		if (!WaitMilliseconds(wait_seconds, &wait_ms) ||
			!AddMilliseconds(now_ms, wait_ms, &result_out->next_think_ms))
			return 0;
	}
	else
	{
		if (!AddMilliseconds(now_ms, law->trigger_remove_delay_ms,
			&result_out->next_think_ms))
			return 0;
	}
	return 1;
}

int SG_HostMechanismTrainStep(const sg_host_mechanism_law_t *law,
	sg_host_mechanism_train_event_t event, uint32_t flags, float wait_seconds,
	int state, int has_target, int has_current_target, int has_damage,
	int other_is_client_or_monster, uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_transition_t *result_out)
{
	if (!result_out || !SG_HostMechanismLawValid(law) || !StateValid(state))
		return 0;
	ClearTransition(result_out);
	result_out->next_state = state;
	if (event == SG_HOST_MECHANISM_TRAIN_BLOCKED)
	{
		if (!other_is_client_or_monster)
		{
			result_out->accepted = 1;
			result_out->destroyed = 1;
			return 1;
		}
		if ((flags & SG_HOST_MECHANISM_TRAIN_BLOCK_STOPS) || !has_damage ||
			now_ms < debounce_until_ms)
			return 1;
		result_out->accepted = 1;
		result_out->damaged = 1;
		if (!AddMilliseconds(now_ms, law->train_blocked_debounce_ms,
			&result_out->next_debounce_ms))
			return 0;
		return 1;
	}
	if (event == SG_HOST_MECHANISM_TRAIN_WAIT)
	{
		uint64_t wait_ms;

		if (!isfinite(wait_seconds))
			return 0;
		result_out->accepted = 1;
		/* train_next marks the path-corner arrival STATE_TOP before
		 * Move_Calc; train_wait therefore always observes the top endpoint. */
		result_out->next_state = SG_HOST_MECHANISM_STATE_TOP;
		if (wait_seconds > 0.0f)
		{
			if (!WaitMilliseconds(wait_seconds, &wait_ms) ||
				!AddMilliseconds(now_ms, wait_ms,
					&result_out->next_think_ms))
				return 0;
		}
		else if (wait_seconds < 0.0f &&
			(flags & SG_HOST_MECHANISM_TRAIN_TOGGLE))
		{
			result_out->stopped = 1;
			result_out->started = 1;
			result_out->next_state = SG_HOST_MECHANISM_STATE_TOP;
		}
		else if (wait_seconds == 0.0f)
		{
			result_out->immediate = 1;
			result_out->started = 1;
			result_out->next_state = SG_HOST_MECHANISM_STATE_TOP;
		}
		return 1;
	}
	if (event == SG_HOST_MECHANISM_TRAIN_USE)
	{
		result_out->accepted = 1;
		if (flags & SG_HOST_MECHANISM_TRAIN_START_ON)
		{
			if (flags & SG_HOST_MECHANISM_TRAIN_TOGGLE)
				result_out->stopped = 1;
			return 1;
		}
		if (has_current_target || has_target)
		{
			result_out->started = 1;
			result_out->next_state = SG_HOST_MECHANISM_STATE_TOP;
		}
		return 1;
	}
	return 0;
}
