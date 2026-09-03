#include "sg_rune_mechanisms.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sg_rune_entities.h"
#include "sg_rune_bsp.h"
#include "sg_rune_artifact.h"
#include "sg_rune_cx_build.h"
#include "sg_rune_flight.h"
#include "sg_rune_locate.h"

#define BODY_HALF 16.0f
#define BODY_ORIGIN 24.0f        /* origin above the feet */
#define DEFAULT_LIP 8.0f
#define DEFAULT_PUSH_SPEED 1000.0f
#define DEFAULT_DOOR_SPEED 100.0f
#define DEFAULT_PLAT_SPEED 20.0f
#define DEFAULT_TRAIN_SPEED 100.0f

/* ---- store ---------------------------------------------------------------- */

void SG_RuneMechStoreInit(sg_rune_mech_store_t *store)
{
	if (store)
		memset(store, 0, sizeof(*store));
}

void SG_RuneMechStoreFree(sg_rune_mech_store_t *store)
{
	if (!store)
		return;
	free(store->records);
	free(store->cells);
	memset(store, 0, sizeof(*store));
}

void SG_RuneMechStoreView(const sg_rune_mech_store_t *store,
	sg_rune_mech_table_t *table_out)
{
	if (!table_out)
		return;
	memset(table_out, 0, sizeof(*table_out));
	if (!store)
		return;
	table_out->records = store->records;
	table_out->record_count = store->record_count;
	table_out->cells = store->cells;
	table_out->cell_count = store->cell_count;
}

static int Grow(void **array, uint32_t *capacity, uint32_t required,
	size_t element)
{
	uint32_t next = *capacity ? *capacity : 64U;
	void *grown;

	if (required <= *capacity)
		return 1;
	while (next < required)
		next *= 2U;
	grown = realloc(*array, (size_t)next * element);
	if (!grown)
		return 0;
	*array = grown;
	*capacity = next;
	return 1;
}

static uint32_t AppendRecord(sg_rune_mech_store_t *store,
	const sg_rune_mech_t *record)
{
	if (!Grow((void **)&store->records, &store->record_capacity,
		store->record_count + 1U, sizeof(*store->records)))
		return SG_RUNE_CX_INDEX_NONE;
	store->records[store->record_count] = *record;
	return store->record_count++;
}

static int AppendCell(sg_rune_mech_store_t *store, uint32_t cell)
{
	if (!Grow((void **)&store->cells, &store->cell_capacity,
		store->cell_count + 1U, sizeof(*store->cells)))
		return 0;
	store->cells[store->cell_count++] = cell;
	return 1;
}

/* ---- the map's entities ----------------------------------------------------- */

typedef struct mover_s
{
	const sg_rune_entity_t *entity;
	uint32_t index;           /* into semantics->entities */
	float mins[3], maxs[3];   /* bounds at spawn, world space */
	float size[3];
	float center[3];
} mover_t;

static float Lip(const sg_rune_entity_t *entity)
{
	return entity->lip_set ? entity->lip :
		DEFAULT_LIP;
}

static void MoverFrom(const sg_rune_entity_t *entity, uint32_t index,
	mover_t *mover)
{
	uint32_t axis;

	memset(mover, 0, sizeof(*mover));
	mover->entity = entity;
	mover->index = index;
	for (axis = 0U; axis < 3U; axis++)
	{
		mover->mins[axis] = entity->mins[axis];
		mover->maxs[axis] = entity->maxs[axis];
		mover->size[axis] = mover->maxs[axis] - mover->mins[axis];
		mover->center[axis] = (mover->mins[axis] + mover->maxs[axis]) * 0.5f;
	}
}

/* A lift spawns at its bottom: how far below its spawn bounds that is. */
static float PlatformDrop(const mover_t *plat)
{
	const sg_rune_entity_t *entity = plat->entity;

	if (entity->height > 0.0f)
		return entity->height;
	return plat->size[2] - Lip(entity);
}

/* A door's open offset from its closed position. */
static void DoorTravel(const mover_t *door, float travel[3])
{
	const sg_rune_entity_t *entity = door->entity;
	float distance = fabsf(entity->move_direction[0]) * door->size[0] +
		fabsf(entity->move_direction[1]) * door->size[1] +
		fabsf(entity->move_direction[2]) * door->size[2] - Lip(entity);
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		travel[axis] = entity->move_direction[axis] * distance;
}

static int IsDoor(const sg_rune_entity_t *e)
{
	return (e->kind == SG_RUNE_ENTITY_DOOR || e->kind == SG_RUNE_ENTITY_SECRET_DOOR) &&
		e->bmodel >= 0 && e->has_bounds;
}

static int IsPlatform(const sg_rune_entity_t *e)
{
	return e->kind == SG_RUNE_ENTITY_PLATFORM && e->bmodel >= 0 && e->has_bounds;
}

/* Buttons and touch triggers: what a body works by pressing or entering. */
static int IsButton(const sg_rune_entity_t *e)
{
	return (e->kind == SG_RUNE_ENTITY_BUTTON || e->kind == SG_RUNE_ENTITY_TRIGGER) &&
		e->has_bounds;
}

static int IsTeleporter(const sg_rune_entity_t *e)
{
	return e->kind == SG_RUNE_ENTITY_TELEPORT_TRIGGER;
}

static int IsPush(const sg_rune_entity_t *e)
{
	return e->kind == SG_RUNE_ENTITY_PUSH && e->has_bounds;
}

static int IsTrain(const sg_rune_entity_t *e)
{
	return e->kind == SG_RUNE_ENTITY_TRAIN && e->bmodel >= 0 && e->has_bounds;
}

/* ---- cells ------------------------------------------------------------------- */

static void CellBox(const sg_rune_cx_cell_t *cell, float mins[3], float maxs[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		mins[axis] = (float)cell->bounds.mins.value[axis] / (float)SG_RUNE_CX_Q8_ONE;
		maxs[axis] = (float)cell->bounds.maxs.value[axis] / (float)SG_RUNE_CX_Q8_ONE;
	}
}

static int Overlaps(const float amins[3], const float amaxs[3],
	const float bmins[3], const float bmaxs[3])
{
	return amins[0] < bmaxs[0] && amaxs[0] > bmins[0] &&
		amins[1] < bmaxs[1] && amaxs[1] > bmins[1] &&
		amins[2] < bmaxs[2] && amaxs[2] > bmins[2];
}

/* Stamps SUPPORTED on the cells whose bottom rests on a surface at height
 * top within the footprint: where a body stands on a lift or a train. */
static void MarkFloorAt(sg_rune_cx_cell_t *cells, uint32_t cell_count,
	const float mins[3], const float maxs[3], float top)
{
	uint32_t cell;
	float fmins[3], fmaxs[3];

	fmins[0] = mins[0] + BODY_HALF;
	fmins[1] = mins[1] + BODY_HALF;
	fmins[2] = top + BODY_ORIGIN - 4.0f;
	fmaxs[0] = maxs[0] - BODY_HALF;
	fmaxs[1] = maxs[1] - BODY_HALF;
	fmaxs[2] = top + BODY_ORIGIN + 8.0f;
	if (fmins[0] >= fmaxs[0] || fmins[1] >= fmaxs[1])
	{
		fmins[0] = mins[0];
		fmins[1] = mins[1];
		fmaxs[0] = maxs[0];
		fmaxs[1] = maxs[1];
	}
	for (cell = 0U; cell < cell_count; cell++)
	{
		float cmins[3], cmaxs[3], probe_mins[3], probe_maxs[3];

		CellBox(&cells[cell], cmins, cmaxs);
		/* The cell's bottom must lie in the band, and its footprint overlap. */
		probe_mins[0] = cmins[0]; probe_mins[1] = cmins[1]; probe_mins[2] = cmins[2];
		probe_maxs[0] = cmaxs[0]; probe_maxs[1] = cmaxs[1];
		probe_maxs[2] = cmins[2] + 0.5f;
		if (Overlaps(probe_mins, probe_maxs, fmins, fmaxs))
			cells[cell].semantics |= SG_RUNE_CX_CELL_SUPPORTED |
				SG_RUNE_CX_CELL_MOVER_VOLUME;
	}
}

/* Where a train stands at a path corner: its mins at the corner. */
static void TrainAtCorner(const mover_t *train, const float corner[3],
	float mins[3], float maxs[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		mins[axis] = corner[axis];
		maxs[axis] = corner[axis] + train->size[axis];
	}
}

static int Semantics(const sg_rune_bsp_t *world, sg_rune_entities_t *store,
	sg_rune_entities_t **semantics_out)
{
	if (!SG_RuneEntitiesParse(world, store))
		return 0;
	*semantics_out = store;
	return 1;
}

int SG_RuneMechMarkCells(const sg_rune_bsp_t *world,
	sg_rune_cx_t *cx)
{
	sg_rune_entities_t store, *semantics = NULL;
	sg_rune_cx_view_t view;
	sg_rune_cx_cell_t *cells;
	uint32_t index;

	if (!world || !cx || !SG_RuneCxRead(cx, &view))
		return 0;
	cells = SG_RuneCxCellsMutable(cx);
	if (!cells)
		return 0;
	if (!Semantics(world, &store, &semantics))
		return 1;   /* a map without readable entities has no mechanisms */
	for (index = 0U; index < semantics->count; index++)
	{
		const sg_rune_entity_t *entity = &semantics->records[index];
		mover_t mover;

		if (IsPlatform(entity))
		{
			float drop;

			MoverFrom(entity, index, &mover);
			drop = PlatformDrop(&mover);
			/* Standing on the lift at rest, and on it raised. */
			MarkFloorAt(cells, view.cell_count, mover.mins, mover.maxs,
				mover.maxs[2] - drop);
			MarkFloorAt(cells, view.cell_count, mover.mins, mover.maxs,
				mover.maxs[2]);
		}
		else if (IsTrain(entity))
		{
			uint32_t corner, first, guard = 0U;

			MoverFrom(entity, index, &mover);
			first = SG_RuneEntitiesFind(semantics, semantics->records[index].pathtarget, 0U);
			if (first == SG_RUNE_CX_INDEX_NONE)
				first = SG_RuneEntitiesTargetOf(semantics, index);
			corner = first;
			while (corner != SG_RUNE_CX_INDEX_NONE &&
				corner < semantics->count && guard++ < 256U)
			{
				const sg_rune_entity_t *stop = &semantics->records[corner];
				float mins[3], maxs[3];
				uint32_t next;

				TrainAtCorner(&mover, stop->origin, mins, maxs);
				MarkFloorAt(cells, view.cell_count, mins, maxs, maxs[2]);
				next = SG_RuneEntitiesTargetOf(semantics, corner);
				if (next == SG_RUNE_CX_INDEX_NONE || next == corner || next == first)
					break;   /* the path ends, or loops back to its start */
				corner = next;
			}
		}
	}
	SG_RuneEntitiesFree(&store);
	return 1;
}

/* ---- pass two: records and crossings ------------------------------------------- */

typedef struct emit_s
{
	const sg_rune_bsp_t *world;
	const sg_rune_entities_t *semantics;
	const sg_rune_cx_t *cx;
	sg_rune_cx_view_t view;
	const sg_rune_law_t *law;
	sg_rune_move_store_t *movement;
	sg_rune_mech_store_t *store;
	sg_rune_artifact_t artifact;   /* a view for the locator */
	sg_rune_locator_t locator;
	uint32_t *record_of_entity;    /* per entity: its mechanism record or NONE */
} emit_t;

static void RecordFrom(const mover_t *mover, sg_rune_mech_kind_t kind,
	sg_rune_mech_t *record)
{
	memset(record, 0, sizeof(*record));
	record->kind = (uint8_t)kind;
	record->activation = SG_RUNE_MECH_ACTIVATE_TOUCH;
	record->bmodel = mover->entity->bmodel;
	record->entity = mover->index;
	record->activator = SG_RUNE_CX_INDEX_NONE;
	if (mover->entity->bmodel >= 0)
		memcpy(record->origin, mover->center, sizeof(record->origin));
	else
		memcpy(record->origin, mover->entity->origin, sizeof(record->origin));
	memcpy(record->mins, mover->mins, sizeof(record->mins));
	memcpy(record->maxs, mover->maxs, sizeof(record->maxs));
	record->speed = mover->entity->speed;
	record->wait = mover->entity->wait;
	record->first_cell = 0U;
	record->cell_count = 0U;
}

/* A door's gate: the cells a body would occupy while inside the closed
 * door's volume.  Recorded as the record's cells, and every crossing that
 * arrives in one goes through the door. */
static int GateDoor(emit_t *emit, uint32_t record_index, const mover_t *door)
{
	sg_rune_mech_t *record = &emit->store->records[record_index];
	float gmins[3], gmaxs[3];
	uint32_t cell, first = emit->store->cell_count;

	gmins[0] = door->mins[0] - BODY_HALF;
	gmins[1] = door->mins[1] - BODY_HALF;
	gmins[2] = door->mins[2] - 32.0f;
	gmaxs[0] = door->maxs[0] + BODY_HALF;
	gmaxs[1] = door->maxs[1] + BODY_HALF;
	gmaxs[2] = door->maxs[2] + BODY_ORIGIN;
	for (cell = 0U; cell < emit->view.cell_count; cell++)
	{
		float cmins[3], cmaxs[3];

		CellBox(&emit->view.cells[cell], cmins, cmaxs);
		if (Overlaps(cmins, cmaxs, gmins, gmaxs) && !AppendCell(emit->store, cell))
			return 0;
	}
	record = &emit->store->records[record_index];
	record->first_cell = first;
	record->cell_count = emit->store->cell_count - first;
	SG_RuneMoveGate(emit->movement, &emit->store->cells[first],
		record->cell_count, record_index);
	return 1;
}

static uint32_t FloorNear(emit_t *emit, const float point[3], float radius)
{
	return SG_RuneLocateNearestFloor(&emit->locator, point, radius, 40.0f);
}

static uint8_t StancesOf(const sg_rune_cx_view_t *view, uint32_t cell)
{
	uint8_t stances = 0U;

	if (view->cells[cell].valid_stances & SG_RUNE_CX_STANCE_STANDING)
		stances |= SG_RUNE_MOVE_STANDING;
	if (view->cells[cell].valid_stances & SG_RUNE_CX_STANCE_CROUCHING)
		stances |= SG_RUNE_MOVE_CROUCHING;
	return stances;
}

static int EmitPlatform(emit_t *emit, const mover_t *plat)
{
	sg_rune_mech_t record;
	uint32_t index, rest, top;
	float drop = PlatformDrop(plat), point[3], seconds;

	RecordFrom(plat, SG_RUNE_MECH_PLATFORM, &record);
	record.travel[2] = drop;
	record.speed = plat->entity->speed > 0.0f ? plat->entity->speed :
		DEFAULT_PLAT_SPEED;
	index = AppendRecord(emit->store, &record);
	if (index == SG_RUNE_CX_INDEX_NONE)
		return 0;
	point[0] = plat->center[0];
	point[1] = plat->center[1];
	point[2] = plat->maxs[2] - drop;
	rest = FloorNear(emit, point, plat->size[0] * 0.5f + plat->size[1] * 0.5f);
	point[2] = plat->maxs[2];
	top = FloorNear(emit, point, plat->size[0] * 0.5f + plat->size[1] * 0.5f);
	if (rest == SG_RUNE_CX_INDEX_NONE || top == SG_RUNE_CX_INDEX_NONE || rest == top)
		return 1;
	seconds = drop / record.speed + 1.0f;
	return SG_RuneMoveAppendMechanism(emit->movement, rest, top,
			SG_RUNE_MOVE_PLATFORM, StancesOf(&emit->view, rest), index, NULL,
			seconds) &&
		SG_RuneMoveAppendMechanism(emit->movement, top, rest,
			SG_RUNE_MOVE_PLATFORM, StancesOf(&emit->view, top), index, NULL,
			seconds + record.wait);
}

static int EmitTeleporter(emit_t *emit, const mover_t *pad)
{
	sg_rune_mech_t record;
	uint32_t index, destination_entity, from, to;
	float point[3];

	destination_entity = SG_RuneEntitiesTargetOf(emit->semantics, pad->index);
	if (destination_entity == SG_RUNE_CX_INDEX_NONE ||
		destination_entity >= emit->semantics->count)
		return 1;
	RecordFrom(pad, SG_RUNE_MECH_TELEPORTER, &record);
	memcpy(record.travel,
		emit->semantics->records[destination_entity].origin,
		sizeof(record.travel));
	index = AppendRecord(emit->store, &record);
	if (index == SG_RUNE_CX_INDEX_NONE)
		return 0;
	memcpy(point, pad->entity->origin, sizeof(point));
	from = FloorNear(emit, point, 48.0f);
	memcpy(point, record.travel, sizeof(point));
	to = FloorNear(emit, point, 64.0f);
	if (from == SG_RUNE_CX_INDEX_NONE || to == SG_RUNE_CX_INDEX_NONE || from == to)
		return 1;
	return SG_RuneMoveAppendMechanism(emit->movement, from, to,
		SG_RUNE_MOVE_TELEPORT, StancesOf(&emit->view, from), index, NULL, 0.5f);
}

static int EmitPush(emit_t *emit, const mover_t *push)
{
	sg_rune_mech_t record;
	uint32_t index, from, start, to;
	float speed = push->entity->speed > 0.0f ? push->entity->speed :
		DEFAULT_PUSH_SPEED;
	float velocity[3], point[3];
	sg_rune_flight_t flight;
	uint32_t axis;

	RecordFrom(push, SG_RUNE_MECH_PUSH, &record);
	for (axis = 0U; axis < 3U; axis++)
	{
		velocity[axis] = push->entity->move_direction[axis] * speed * 10.0f;
		record.travel[axis] = velocity[axis];
	}
	index = AppendRecord(emit->store, &record);
	if (index == SG_RUNE_CX_INDEX_NONE)
		return 0;
	point[0] = push->center[0];
	point[1] = push->center[1];
	point[2] = push->mins[2] + BODY_ORIGIN;
	from = FloorNear(emit, point, push->size[0] * 0.5f + push->size[1] * 0.5f + 32.0f);
	start = SG_RuneLocate(&emit->locator, point, SG_RUNE_MOVE_STANDING, 16.0f, NULL);
	if (from == SG_RUNE_CX_INDEX_NONE || start == SG_RUNE_CX_INDEX_NONE)
		return 1;
	if (!SG_RuneFlightTrace(&emit->view, emit->law, start, point, velocity,
			&flight) || (flight.outcome != SG_RUNE_FLIGHT_LANDED &&
			flight.outcome != SG_RUNE_FLIGHT_WATER))
		return 1;
	to = flight.landing_cell;
	if (!(emit->view.cells[to].semantics & SG_RUNE_CX_CELL_SUPPORTED) ||
		to == from)
		return 1;
	return SG_RuneMoveAppendMechanism(emit->movement, from, to,
		SG_RUNE_MOVE_EXTERNAL_FORCE, StancesOf(&emit->view, from), index,
		velocity, flight.seconds);
}

static int EmitTrain(emit_t *emit, const mover_t *train)
{
	sg_rune_mech_t record;
	uint32_t index, corner, first, previous_cell = SG_RUNE_CX_INDEX_NONE;
	float speed = train->entity->speed > 0.0f ? train->entity->speed :
		DEFAULT_TRAIN_SPEED;
	float previous_point[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t guard = 0U;

	RecordFrom(train, SG_RUNE_MECH_TRAIN, &record);
	record.speed = speed;
	index = AppendRecord(emit->store, &record);
	if (index == SG_RUNE_CX_INDEX_NONE)
		return 0;
	first = SG_RuneEntitiesFind(emit->semantics, emit->semantics->records[train->index].pathtarget, 0U);
	if (first == SG_RUNE_CX_INDEX_NONE)
		first = SG_RuneEntitiesTargetOf(emit->semantics, train->index);
	corner = first;
	while (corner != SG_RUNE_CX_INDEX_NONE &&
		corner < emit->semantics->count && guard++ < 256U)
	{
		const sg_rune_entity_t *stop = &emit->semantics->records[corner];
		float mins[3], maxs[3], point[3];
		uint32_t cell, next;

		TrainAtCorner(train, stop->origin, mins, maxs);
		point[0] = (mins[0] + maxs[0]) * 0.5f;
		point[1] = (mins[1] + maxs[1]) * 0.5f;
		point[2] = maxs[2];
		cell = FloorNear(emit, point, train->size[0] * 0.5f + train->size[1] * 0.5f);
		if (cell != SG_RUNE_CX_INDEX_NONE && previous_cell != SG_RUNE_CX_INDEX_NONE &&
			cell != previous_cell)
		{
			float dx = point[0] - previous_point[0], dy = point[1] - previous_point[1];
			float dz = point[2] - previous_point[2];
			float seconds = sqrtf(dx * dx + dy * dy + dz * dz) / speed;

			if (!SG_RuneMoveAppendMechanism(emit->movement, previous_cell, cell,
				SG_RUNE_MOVE_TRAIN, StancesOf(&emit->view, previous_cell), index,
				NULL, seconds + 1.0f))
				return 0;
		}
		if (cell != SG_RUNE_CX_INDEX_NONE)
		{
			previous_cell = cell;
			memcpy(previous_point, point, sizeof(point));
		}
		next = SG_RuneEntitiesTargetOf(emit->semantics, corner);
		if (next == SG_RUNE_CX_INDEX_NONE || next == corner)
			break;
		corner = next;
		if (corner == first)
			break;
	}
	return 1;
}

static int EmitButton(emit_t *emit, const mover_t *button)
{
	sg_rune_mech_t record;
	uint32_t index;

	RecordFrom(button, SG_RUNE_MECH_BUTTON, &record);
	record.activation = button->entity->health > 0 ? SG_RUNE_MECH_ACTIVATE_SHOT :
		SG_RUNE_MECH_ACTIVATE_TOUCH;
	index = AppendRecord(emit->store, &record);
	if (index == SG_RUNE_CX_INDEX_NONE)
		return 0;
	emit->record_of_entity[button->index] = index;
	return 1;
}

static int EmitDoor(emit_t *emit, const mover_t *door)
{
	sg_rune_mech_t record;
	uint32_t index, worker;

	RecordFrom(door, SG_RUNE_MECH_DOOR, &record);
	DoorTravel(door, record.travel);
	record.speed = door->entity->speed > 0.0f ? door->entity->speed :
		DEFAULT_DOOR_SPEED;
	/* Who works it: a button or trigger that targets it, its own area
	 * trigger, damage, or nothing. */
	record.activation = SG_RUNE_MECH_ACTIVATE_TOUCH;
	worker = SG_RuneEntitiesTargetedBy(emit->semantics, door->index, 0U);
	while (worker != SG_RUNE_CX_INDEX_NONE)
	{
		if (emit->record_of_entity[worker] != SG_RUNE_CX_INDEX_NONE)
		{
			record.activation = SG_RUNE_MECH_ACTIVATE_TARGETED;
			record.activator = emit->record_of_entity[worker];
			break;
		}
		worker = SG_RuneEntitiesTargetedBy(emit->semantics, door->index, worker + 1U);
	}
	if (worker == SG_RUNE_CX_INDEX_NONE && door->entity->health > 0)
		record.activation = SG_RUNE_MECH_ACTIVATE_SHOT;
	else if (worker == SG_RUNE_CX_INDEX_NONE && door->entity->targetname[0])
		record.activation = SG_RUNE_MECH_ACTIVATE_NONE;   /* worked by something unknown */
	index = AppendRecord(emit->store, &record);
	if (index == SG_RUNE_CX_INDEX_NONE)
		return 0;
	emit->record_of_entity[door->index] = index;
	return GateDoor(emit, index, door);
}

int SG_RuneMechEmit(const sg_rune_bsp_t *world,
	const sg_rune_cx_t *cx, const sg_rune_law_t *law,
	sg_rune_move_store_t *movement, sg_rune_mech_store_t *store)
{
	emit_t emit;
	sg_rune_entities_t parsed, *semantics = NULL;
	uint32_t index;
	int ok = 0;

	if (!world || !cx || !law || !movement || !store)
		return 0;
	memset(&emit, 0, sizeof(emit));
	if (!SG_RuneCxRead(cx, &emit.view))
		return 0;
	if (!Semantics(world, &parsed, &semantics))
		return 1;
	emit.world = world;
	emit.semantics = semantics;
	emit.cx = cx;
	emit.law = law;
	emit.movement = movement;
	emit.store = store;
	emit.artifact.cx = emit.view;
	emit.artifact.law = *law;
	emit.record_of_entity = malloc((size_t)(semantics->count ?
		semantics->count : 1U) * sizeof(uint32_t));
	if (!emit.record_of_entity || !SG_RuneLocatorBuild(&emit.locator, &emit.artifact))
		goto done;
	for (index = 0U; index < semantics->count; index++)
		emit.record_of_entity[index] = SG_RUNE_CX_INDEX_NONE;
	/* Buttons and triggers first, so doors can name their workers. */
	for (index = 0U; index < semantics->count; index++)
	{
		const sg_rune_entity_t *entity = &semantics->records[index];
		mover_t mover;

		if (!IsButton(entity) || IsDoor(entity))
			continue;
		MoverFrom(entity, index, &mover);
		if (!EmitButton(&emit, &mover))
			goto done;
	}
	for (index = 0U; index < semantics->count; index++)
	{
		const sg_rune_entity_t *entity = &semantics->records[index];
		mover_t mover;
		int fine = 1;

		MoverFrom(entity, index, &mover);
		if (IsDoor(entity))
			fine = EmitDoor(&emit, &mover);
		else if (IsPlatform(entity))
			fine = EmitPlatform(&emit, &mover);
		else if (IsTeleporter(entity))
			fine = EmitTeleporter(&emit, &mover);
		else if (IsPush(entity))
			fine = EmitPush(&emit, &mover);
		else if (IsTrain(entity))
			fine = EmitTrain(&emit, &mover);
		if (!fine)
			goto done;
	}
	ok = 1;
done:
	SG_RuneLocatorFree(&emit.locator);
	free(emit.record_of_entity);
	SG_RuneEntitiesFree(&parsed);
	return ok;
}
