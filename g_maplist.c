#include "g_local.h"

void MapList_Configure(MapInfo maps[], int count, qboolean random_maps)
{
	if (!maps || count <= 1 || !random_maps)
		return;

	/* Preserve the historical alphabetical preparation for random-map mode.
	 * Sequential rotation must retain the administrator's file order. */
	SortMaplist(maps, 0, count - 1);
}

qboolean MapList_SequentialStartup(const MapInfo maps[], int count,
	const char *current_map, int *next_index, const MapInfo **startup_map)
{
	if (!maps || count <= 0 || !maps[0].mapname || !current_map ||
		!next_index || !startup_map)
		return false;

	*next_index = count > 1 ? 1 : 0;
	*startup_map = strcmp(maps[0].mapname, current_map) ? &maps[0] : NULL;
	return true;
}

const MapInfo *MapList_SelectNext(const MapInfo maps[], int count,
	int *next_index, qboolean random_maps, const MapInfo *random_map)
{
	const MapInfo *selected;

	/* Random selection owns its own policy and must not consume or modify the
	 * sequential cursor. */
	if (random_maps)
		return random_map;

	if (!maps || count <= 0 || !next_index)
		return NULL;

	if (*next_index < 0 || *next_index >= count)
		*next_index = 0;

	selected = &maps[*next_index];
	(*next_index)++;
	if (*next_index == count)
		*next_index = 0;

	return selected;
}

void SortMaplist(MapInfo arr[], int min, int max)
{
	if (min < max)
	{
		int ndx = MapDivide(arr, min, max);
		SortMaplist(arr, min, ndx - 1);
		SortMaplist(arr, ndx + 1, max);
	}
}

int MapDivide(MapInfo arr[], int min, int max)
{
	MapInfo tmp = arr[max];
	int nndx = min - 1;
	int x;

	for (x = min; x <= max - 1; x++)
	{
		if (strcmp(arr[x].mapname, tmp.mapname) < 0)
		{
			nndx++;
			flip(&arr[nndx], &arr[x]);
		}
	}
	flip(&arr[nndx + 1], &arr[max]);
	return nndx + 1;
}

void flip(MapInfo *x, MapInfo *y)
{
	MapInfo tmp = *x;
	*x = *y;
	*y = tmp;
}
