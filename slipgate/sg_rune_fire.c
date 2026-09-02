#include "sg_rune_fire.h"

#include "sg_rune_cx.h"

#include <stdlib.h>
#include <string.h>


/* ---- store ---------------------------------------------------------------------- */

void SG_RuneFireStoreInit(sg_rune_fire_store_t *store)
{
	if (store)
		memset(store, 0, sizeof(*store));
}

void SG_RuneFireStoreFree(sg_rune_fire_store_t *store)
{
	if (!store)
		return;
	free(store->cells);
	free(store->records);
	memset(store, 0, sizeof(*store));
}

void SG_RuneFireStoreView(const sg_rune_fire_store_t *store,
	sg_rune_fire_table_t *table_out)
{
	if (!table_out)
		return;
	memset(table_out, 0, sizeof(*table_out));
	if (!store)
		return;
	table_out->cells = store->cells;
	table_out->cell_count = store->cell_count;
	table_out->records = store->records;
	table_out->record_count = store->record_count;
}

uint32_t SG_RuneFireFlags(const sg_rune_fire_table_t *table, uint32_t cell,
	uint32_t target)
{
	const sg_rune_fire_cell_t *row;
	uint32_t low, high;

	if (!table || !table->cells || cell >= table->cell_count ||
		target >= table->cell_count)
		return 0U;
	row = &table->cells[cell];
	target = table->cells[target].representative;
	if (target == SG_RUNE_CX_INDEX_NONE)
		return 0U;
	low = row->first;
	high = row->first + row->count;
	if (high > table->record_count)
		return 0U;
	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		const sg_rune_fire_t *record = &table->records[middle];

		if (record->target == target)
			return record->flags;
		if (record->target < target)
			low = middle + 1U;
		else
			high = middle;
	}
	return 0U;
}

