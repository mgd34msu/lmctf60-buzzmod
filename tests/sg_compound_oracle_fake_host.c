#include "sg_compound_oracle_fixture.h"

trace_t HostTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int mask)
{
	static csurface_t sky_surface;
	trace_t trace;

	(void)maxs; (void)passent;
	memset(&trace, 0, sizeof(trace));
	fixture_observation.trace_calls++;
	trace.fraction = 1.0f;
	trace.ent = &fixture_edicts[0];
	if (fixture_config.suffix_nonfinite &&
	    fixture_observation.top_staged)
	{
		trace.endpos[0] = NAN;
		return trace;
	}
	if (fixture_config.loader_transient &&
	    fixture_edicts[5].solid == SOLID_NOT &&
	    fixture_edicts[6].solid == SOLID_NOT)
	{
		if (!mins && mask == MASK_SHOT)
			fixture_observation.transient_masked_shot_traces++;
		if (!mins && mask == MASK_PLAYERSOLID)
			fixture_observation.transient_masked_contact_traces++;
	}
	if (fixture_config.loader_unowned &&
	    fixture_edicts[7].solid == SOLID_BBOX)
	{
		trace.fraction = 0.5f;
		trace.ent = &fixture_edicts[7];
		return trace;
	}
	if (fixture_config.hook_suffix && fixture_observation.top_staged &&
	    fixture_config.hook_discover_control && !mins &&
	    mask == MASK_PLAYERSOLID)
	{
		vec3_t delta;

		VectorSubtract(end, start, delta);
		if (VectorLength(delta) > 500.0f)
		{
			trace.fraction = 0.5f;
			VectorCopy(start, trace.endpos);
			trace.endpos[1] =
				fixture_observation.last_hook_top_hold.origin[1] * 0.125f;
			trace.endpos[2] = 26.0f;
			return trace;
		}
	}
	if (fixture_config.hook_suffix && fixture_observation.top_staged &&
	    !mins && mask == MASK_SHOT)
	{
		vec3_t delta;

		fixture_observation.hook_started = true;
		VectorSubtract(end, start, delta);
		if (VectorLength(delta) <= 200.0f &&
		    fixture_config.hook_muzzle_blocked)
			trace.fraction = 0.5f;
		else if (VectorLength(delta) > 200.0f)
		{
			trace.fraction = 0.5f;
			VectorCopy(fixture_config.hook_bite, trace.endpos);
			if (fixture_config.hook_shot_sky)
			{
				memset(&sky_surface, 0, sizeof(sky_surface));
				sky_surface.flags = SURF_SKY;
				trace.surface = &sky_surface;
			}
			if (fixture_config.hook_shot_nonworld)
				trace.ent = &fixture_edicts[4];
		}
		return trace;
	}
	if (!mins && !fixture_observation.top_staged)
		fixture_observation.pretop_contact_traces++;
	if (mins)
	{
		fixture_observation.last_pmove_mask = mask;
		if (mask == (MASK_PLAYERSOLID & ~CONTENTS_MONSTER))
			fixture_observation.stripped_pmove_masks++;
		if (mask == MASK_PLAYERSOLID)
			fixture_observation.normal_pmove_masks++;
		trace.fraction = 0.5f;
		trace.ent = &fixture_edicts[1];
	}
	return trace;
}

int HostPointContents(const vec3_t point)
{
	(void)point;
	return CONTENTS_WATER;
}

int HostBoxEdicts(const vec3_t mins, const vec3_t maxs,
	edict_t **list, int max_count, int area_type)
{
	int count = 0;

	(void)mins; (void)maxs;
	if (max_count <= 0)
		return 0;
	if (area_type == AREA_TRIGGERS && !fixture_observation.top_staged &&
	    maxs[0] > fixture_edicts[2].absmin[0] &&
	    mins[0] < fixture_edicts[2].absmax[0] &&
	    maxs[1] > fixture_edicts[2].absmin[1] &&
	    mins[1] < fixture_edicts[2].absmax[1] &&
	    maxs[2] > fixture_edicts[2].absmin[2] &&
	    mins[2] < fixture_edicts[2].absmax[2])
	{
		list[count++] = &fixture_edicts[2];
		if (fixture_config.contaminate_trigger && count < max_count)
			list[count++] = &fixture_edicts[3];
	}
	if (area_type == AREA_TRIGGERS && fixture_config.force_foreign_trigger &&
	    count < max_count)
		list[count++] = &fixture_edicts[3];
	if (area_type == AREA_TRIGGERS && fixture_observation.top_staged &&
	    (fixture_config.hook_bolt_trigger ||
	     fixture_config.suffix_foreign_trigger) && count < max_count)
		list[count++] = &fixture_edicts[3];
	if (area_type == AREA_SOLID)
	{
		list[count++] = &fixture_edicts[1];
		if ((fixture_config.contaminate_solid ||
		     (fixture_observation.top_staged &&
		      fixture_config.suffix_foreign_solid)) && count < max_count)
			list[count++] = &fixture_edicts[4];
	}
	return count;
}

void HostLinkEntity(edict_t *entity)
{
	int index = fixture_observation.link_calls;
	float radius = 0.0f;
	qboolean rotated_bsp;
	int axis;

	if (index < (int)(sizeof(fixture_observation.link_origins) /
	                  sizeof(fixture_observation.link_origins[0])))
		fixture_observation.link_origins[index] = entity->s.origin[0];
	fixture_observation.link_calls++;
	fixture_observation.stage_started = true;
	entity->area.prev = &fixture_edicts[0].area;
	entity->area.next = &fixture_edicts[0].area;
	if (entity->s.origin[0] == 80.0f)
		fixture_observation.top_staged = true;
	entity->linkcount++;
	rotated_bsp = entity->solid == SOLID_BSP &&
	    (entity->s.angles[0] != 0.0f || entity->s.angles[1] != 0.0f ||
	     entity->s.angles[2] != 0.0f);
	if (rotated_bsp)
		for (axis = 0; axis < 3; axis++)
		{
			float lo = fabsf(entity->mins[axis]);
			float hi = fabsf(entity->maxs[axis]);

			if (lo > radius) radius = lo;
			if (hi > radius) radius = hi;
		}
	for (axis = 0; axis < 3; axis++)
	{
		entity->absmin[axis] = entity->s.origin[axis] +
		    (rotated_bsp ? -radius : entity->mins[axis]) - 1.0f;
		entity->absmax[axis] = entity->s.origin[axis] +
		    (rotated_bsp ? radius : entity->maxs[axis]) + 1.0f;
	}
	VectorSubtract(entity->maxs, entity->mins, entity->size);
}

void PublishDoorCompletion(edict_t *door,
	sg_mover_completion_kind_t kind)
{
	level.current_entity = (door->flags & FL_TEAMSLAVE)
	    ? door->teammaster : door;
	SG_MoverCompletionTransition(door);
	door->moveinfo.endfunc = kind == SG_MOVER_COMPLETION_TOP
	    ? door_hit_top : door_hit_bottom;
	SG_MoverCompletionArm(door);
	SG_MoverCompletionDispatch(door);
}

int SuffixX(void)
{
	int command = fixture_observation.suffix_commands;

	switch (fixture_config.suffix)
	{
	case FIXTURE_SUFFIX_SUCCESS:
		if (command <= 4)
			return 80;
		if (command < 8)
			return 0;
		if (command == 8)
			return -40;
		return command < 12 ? -60 : -80;
	case FIXTURE_SUFFIX_NO_SWEEP:
		return 160;
	case FIXTURE_SUFFIX_REENTRY:
		if (command <= 4)
			return 80;
		if (command < 8)
			return 0;
		if (command == 8)
			return -40;
		return 0;
	case FIXTURE_SUFFIX_ARRIVE_BEFORE_CLEAR:
		return command < 4 ? 100 : 80;
	case FIXTURE_SUFFIX_ALWAYS_OUTSIDE:
		if (command <= 4)
			return 200;
		return command < 8 ? 220 : 240;
	case FIXTURE_SUFFIX_BETWEEN_RECROSS:
		if (command <= 4)
			return 80;
		if (command < 8)
			return 0;
		if (command == 8)
			return -40;
		return 120;
	case FIXTURE_SUFFIX_PRECLEAR_CHORD:
		if (command == 0)
			return 80;
		if (command <= 3)
			return 120;
		return -80;
	case FIXTURE_SUFFIX_POSTCLEAR_CHORD:
		if (command <= 1)
			return 80;
		if (command <= 7)
			return 120;
		return -80;
	default:
		return 60;
	}
}

void HostPmove(pmove_t *pmove)
{
	vec3_t start, mins, maxs, end;
	int x = 60;

	fixture_observation.pmove_calls++;
	if (fixture_observation.pmove_calls == 1)
		fixture_observation.first_snapinitial = pmove->snapinitial;
	else if (pmove->snapinitial)
		fixture_observation.later_snapinitial++;
	Set3(start, pmove->s.origin[0] * 0.125f,
	     pmove->s.origin[1] * 0.125f,
	     pmove->s.origin[2] * 0.125f);
	VectorCopy(start, end);
	Set3(mins, -16.0f, -16.0f, -24.0f);
	Set3(maxs, 16.0f, 16.0f, 32.0f);
	(void)pmove->trace(start, mins, maxs, end);

	pmove->groundentity = NULL;
	pmove->watertype = CONTENTS_WATER;
	pmove->waterlevel = 3;
	if (fixture_observation.top_staged)
	{
		qboolean hook_top_hold = fixture_config.hook_suffix &&
		    !fixture_observation.hook_started;

		if (!fixture_observation.first_top_seen)
		{
			fixture_observation.first_top_seen = true;
			fixture_observation.first_top_command = pmove->cmd;
		}
		if (hook_top_hold)
		{
			fixture_observation.hook_top_hold_commands++;
			if (CommandZero(&pmove->cmd))
			{
				fixture_observation.hook_top_zero_commands++;
				pmove->s.origin[1] += 8;
			}
			else
			{
				fixture_observation.hook_top_corrective_commands++;
				pmove->s.origin[1] = 0;
			}
		}
		else
			fixture_observation.suffix_commands++;
		if (fixture_config.hook_suffix)
		{
			if (hook_top_hold)
				x = 160;
			else if (fixture_config.hook_sweep_mode ==
			        SG_HOOK_ORACLE_SWEEP_PRECLEAR_CROSS &&
			    fixture_observation.suffix_commands == 3)
				x = -80;
			else if (fixture_config.hook_sweep_mode ==
			             SG_HOOK_ORACLE_SWEEP_POSTCLEAR_RECROSS &&
			         fixture_observation.suffix_commands == 5)
				x = 80;
			else
				x = fixture_observation.suffix_commands <= 8 ? 160 : 280;
			if (fixture_observation.suffix_commands > 8)
			{
				pmove->watertype = 0;
				pmove->waterlevel = 0;
				pmove->groundentity = &fixture_edicts[0];
			}
		}
		else
			x = SuffixX();
		if (fixture_config.top_drift_at_command > 0 &&
		    fixture_config.top_drift_at_command ==
		    fixture_observation.suffix_commands)
			fixture_edicts[1].velocity[0] = 1.0f;
		if (fixture_config.identity_drift_at_command > 0 &&
		    fixture_config.identity_drift_at_command ==
		    fixture_observation.suffix_commands)
			fixture_edicts[1].s.number = 9;
		if (!hook_top_hold)
			pmove->s.velocity[0] = 64;
		if (hook_top_hold)
			fixture_observation.last_hook_top_hold = pmove->s;
		if (fixture_config.drop_suffix)
		{
			qboolean wet = x <= -60 ||
			    (fixture_config.suffix == FIXTURE_SUFFIX_ARRIVE_BEFORE_CLEAR &&
			     x <= 80);

			pmove->watertype = wet ? CONTENTS_WATER : 0;
			pmove->waterlevel = wet ? 3 : 0;
		}
		if (fixture_config.suffix_hazard)
		{
			pmove->watertype = CONTENTS_LAVA;
			pmove->waterlevel = 2;
		}
		if (fixture_config.suffix_fall &&
		    fixture_observation.suffix_commands < 16)
		{
			pmove->s.velocity[2] = -5600;
			pmove->groundentity = NULL;
		}
		if (fixture_config.suffix_fall &&
		    fixture_observation.suffix_commands == 16)
		{
			pmove->s.velocity[2] = 0;
			pmove->watertype = 0;
			pmove->waterlevel = 0;
			pmove->groundentity = &fixture_edicts[0];
		}
	}
	else if (!CommandZero(&pmove->cmd))
	{
		int contact_cycle;

		fixture_observation.approach_commands++;
		contact_cycle = fixture_config.touch_substep > 0 ?
			(fixture_observation.approach_commands /
			 fixture_config.touch_substep) : 0;
		if (fixture_config.touch_substep > 0 &&
		    (fixture_observation.approach_commands %
		     fixture_config.touch_substep) == 0)
			x = (int)fixture_config.mechanism_x -
			    ((fixture_config.wrong_contact ||
			      (fixture_config.unstable_contact &&
			       (contact_cycle % 2) == 0)) ? 8 : 0);
		else
			x = fixture_config.source_x < -24.0f ?
			    (int)fixture_config.mechanism_x : 180;
	}
	else
	{
		fixture_observation.zero_commands++;
		x = pmove->s.origin[0] / 8;
		if (fixture_observation.stage_started)
		{
			fixture_observation.ride_zero_commands++;
			if (fixture_observation.ride_zero_commands == 1 &&
			    fixture_config.opening_drift)
				x = 80;
			if (fixture_observation.ride_zero_commands == 4 &&
			    fixture_config.hazard_ride)
			{
				pmove->watertype = CONTENTS_LAVA;
				pmove->waterlevel = 2;
			}
			if (fixture_observation.ride_zero_commands == 4 &&
			    fixture_config.fall_ride)
			{
				pmove->s.velocity[2] = 0;
				pmove->watertype = 0;
				pmove->waterlevel = 0;
				pmove->groundentity = &fixture_edicts[0];
			}
		}
		else if (fixture_config.source_hazard)
		{
			pmove->watertype = CONTENTS_LAVA;
			pmove->waterlevel = 2;
		}
		else if (fixture_config.source_dry)
		{
			pmove->watertype = 0;
			pmove->waterlevel = 0;
		}
	}
	pmove->s.origin[0] = (short)(x * 8);
}
