#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum hook_event {
	HOOK_EVENT_FORCE_RELEASE,
	HOOK_EVENT_PULL,
	HOOK_EVENT_TRACE_RELEASE,
	HOOK_EVENT_ABORT,
	HOOK_EVENT_WEAPON_GENERIC,
	HOOK_EVENT_BEGIN_REFIRE,
	HOOK_EVENT_FIRE,
	HOOK_EVENT_IMMEDIATE_TOUCH
};

struct hook_state {
	bool attack;
	bool rope_live;
	bool grounded;
	bool weapon_firing;
	int gunframe;
	int velocity_z;
	int oldvelocity_z;
	enum hook_event events[16];
	size_t event_count;
};

struct fire_result {
	bool rope_live;
	bool touch_called;
	bool plane_valid;
	int flight_steps;
};

static void record_event(struct hook_state *state, enum hook_event event)
{
	assert(state->event_count < sizeof(state->events) / sizeof(state->events[0]));
	state->events[state->event_count++] = event;
}

static void selected_unhook_base(struct hook_state *state)
{
	record_event(state, HOOK_EVENT_FORCE_RELEASE);
}

static void selected_unhook_regression(struct hook_state *state)
{
	record_event(state, HOOK_EVENT_TRACE_RELEASE);
	record_event(state, HOOK_EVENT_ABORT);
	state->rope_live = false;
	state->weapon_firing = false;
	record_event(state, HOOK_EVENT_FORCE_RELEASE);
}

static void weapon_hook_base(struct hook_state *state)
{
	if (!state->attack)
	{
		record_event(state, HOOK_EVENT_TRACE_RELEASE);
		record_event(state, HOOK_EVENT_ABORT);
		state->rope_live = false;
		state->weapon_firing = false;
		if (state->grounded)
		{
			state->velocity_z = 0;
			state->oldvelocity_z = 0;
		}
	}
	else if (state->rope_live)
	{
		record_event(state, HOOK_EVENT_PULL);
		state->velocity_z = 640;
		state->oldvelocity_z = 640;
	}

	record_event(state, HOOK_EVENT_WEAPON_GENERIC);
	if (state->attack && !state->weapon_firing)
	{
		record_event(state, HOOK_EVENT_BEGIN_REFIRE);
		state->weapon_firing = true;
		state->gunframe = 10;
		record_event(state, HOOK_EVENT_FIRE);
		state->rope_live = true;
	}
	else if (state->attack && state->weapon_firing && state->gunframe == 10)
	{
		record_event(state, HOOK_EVENT_FIRE);
		state->rope_live = true;
	}
}

static struct fire_result fire_base(bool immediate_hit)
{
	struct fire_result result = {true, false, false, 0};

	if (immediate_hit)
		result.touch_called = true;
	return result;
}

static struct fire_result fire_with_safe_plane(bool immediate_hit)
{
	struct fire_result result = {true, false, false, 0};

	if (immediate_hit)
	{
		result.touch_called = true;
		result.plane_valid = true;
	}
	return result;
}

static void ordinary_flight_collision(struct fire_result *result)
{
	result->flight_steps++;
	result->touch_called = true;
	result->plane_valid = true;
}

static void assert_events(const struct hook_state *state,
	const enum hook_event *expected, size_t expected_count)
{
	assert(state->event_count == expected_count);
	assert(memcmp(state->events, expected,
	    expected_count * sizeof(expected[0])) == 0);
}

static void test_selected_release_keeps_base_cadence(void)
{
	static const enum hook_event expected[] = {
		HOOK_EVENT_FORCE_RELEASE,
		HOOK_EVENT_PULL,
		HOOK_EVENT_WEAPON_GENERIC,
		HOOK_EVENT_FIRE,
		HOOK_EVENT_TRACE_RELEASE,
		HOOK_EVENT_ABORT,
		HOOK_EVENT_WEAPON_GENERIC
	};
	struct hook_state state = {true, true, false, true, 10, 120, 120,
	    {0}, 0};

	selected_unhook_base(&state);
	assert(state.rope_live);
	weapon_hook_base(&state);
	assert(state.rope_live);
	assert(state.velocity_z == 640);
	state.attack = false;
	weapon_hook_base(&state);
	assert(!state.rope_live);
	assert(state.velocity_z == 640);
	assert(state.oldvelocity_z == 640);
	assert_events(&state, expected, sizeof(expected) / sizeof(expected[0]));
}

static void test_release_preserves_refire_window(void)
{
	static const enum hook_event expected[] = {
		HOOK_EVENT_FORCE_RELEASE,
		HOOK_EVENT_PULL,
		HOOK_EVENT_WEAPON_GENERIC,
		HOOK_EVENT_FIRE,
		HOOK_EVENT_TRACE_RELEASE,
		HOOK_EVENT_ABORT,
		HOOK_EVENT_WEAPON_GENERIC,
		HOOK_EVENT_WEAPON_GENERIC,
		HOOK_EVENT_BEGIN_REFIRE,
		HOOK_EVENT_FIRE
	};
	struct hook_state state = {true, true, false, true, 10, 120, 120,
	    {0}, 0};

	selected_unhook_base(&state);
	weapon_hook_base(&state);
	state.attack = false;
	weapon_hook_base(&state);
	assert(!state.rope_live);
	state.attack = true;
	weapon_hook_base(&state);
	assert(state.weapon_firing);
	assert(state.gunframe == 10);
	assert(state.rope_live);
	assert_events(&state, expected, sizeof(expected) / sizeof(expected[0]));
}

static void test_premature_abort_is_observably_wrong(void)
{
	static const enum hook_event wrong_prefix[] = {
		HOOK_EVENT_TRACE_RELEASE,
		HOOK_EVENT_ABORT,
		HOOK_EVENT_FORCE_RELEASE,
		HOOK_EVENT_WEAPON_GENERIC,
		HOOK_EVENT_BEGIN_REFIRE,
		HOOK_EVENT_FIRE
	};
	struct hook_state state = {true, true, false, true, 10, 120, 120,
	    {0}, 0};

	selected_unhook_regression(&state);
	weapon_hook_base(&state);
	assert(state.rope_live);
	assert(state.velocity_z == 120);
	assert_events(&state, wrong_prefix,
	    sizeof(wrong_prefix) / sizeof(wrong_prefix[0]));
}

static void test_grounded_release_keeps_base_abort_state(void)
{
	struct hook_state state = {false, true, true, true, 10, 640, 640,
	    {0}, 0};

	weapon_hook_base(&state);
	assert(!state.rope_live);
	assert(state.velocity_z == 0);
	assert(state.oldvelocity_z == 0);
}

static void test_plane_exception_is_immediate_only(void)
{
	struct fire_result base_flight = fire_base(false);
	struct fire_result safe_flight = fire_with_safe_plane(false);
	struct fire_result base_hit = fire_base(true);
	struct fire_result safe_hit = fire_with_safe_plane(true);

	assert(base_flight.rope_live == safe_flight.rope_live);
	assert(base_flight.touch_called == safe_flight.touch_called);
	assert(base_flight.plane_valid == safe_flight.plane_valid);
	assert(base_flight.flight_steps == safe_flight.flight_steps);
	ordinary_flight_collision(&base_flight);
	ordinary_flight_collision(&safe_flight);
	assert(base_flight.rope_live == safe_flight.rope_live);
	assert(base_flight.touch_called == safe_flight.touch_called);
	assert(base_flight.plane_valid == safe_flight.plane_valid);
	assert(base_flight.flight_steps == safe_flight.flight_steps);

	assert(base_hit.rope_live == safe_hit.rope_live);
	assert(base_hit.touch_called == safe_hit.touch_called);
	assert(base_hit.flight_steps == safe_hit.flight_steps);
	assert(!base_hit.plane_valid);
	assert(safe_hit.plane_valid);
}

int main(void)
{
	test_selected_release_keeps_base_cadence();
	test_release_preserves_refire_window();
	test_premature_abort_is_observably_wrong();
	test_grounded_release_keeps_base_abort_state();
	test_plane_exception_is_immediate_only();
	puts("sg_human_hook_release_regression_test: ok");
	return 0;
}
