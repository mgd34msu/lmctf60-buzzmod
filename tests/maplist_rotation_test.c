#include <stdio.h>
#include <string.h>

#include "g_local.h"

#define MAP_COUNT 20

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static const char *configured_names[MAP_COUNT] = {
	"lmctf09", "lmctf57", "smap05", "smap30", "lmctf48",
	"smap33", "lmctf32", "lmctf22", "lmctf41", "mactf06",
	"xmap20", "lmctf44", "lmctf35", "lmctf52", "lmctf58",
	"lmctf39", "lmctf26", "smap26", "lmctf54", "lmctf31"
};

static void populate(MapInfo maps[MAP_COUNT])
{
	int i;

	memset(maps, 0, sizeof(*maps) * MAP_COUNT);
	for (i = 0; i < MAP_COUNT; i++)
	{
		maps[i].mapname = (char *)configured_names[i];
		maps[i].minplayers = 0;
		maps[i].maxplayers = 99;
	}
}

static void test_startup_order_and_wrap(void)
{
	MapInfo maps[MAP_COUNT];
	const MapInfo *startup_map = NULL;
	const MapInfo *selected;
	int next_index = -1;
	int transition;
	int i;

	populate(maps);
	MapList_Configure(maps, MAP_COUNT, false);
	for (i = 0; i < MAP_COUNT; i++)
		CHECK(strcmp(maps[i].mapname, configured_names[i]) == 0);

	CHECK(MapList_SequentialStartup(maps, MAP_COUNT, configured_names[0],
		&next_index, &startup_map));
	CHECK(startup_map == NULL);
	CHECK(next_index == 1);

	/* Entry zero is already resident. Twenty native transitions must visit
	 * entries 1..19, then wrap to entry zero exactly once. */
	for (transition = 1; transition <= MAP_COUNT; transition++)
	{
		selected = MapList_SelectNext(maps, MAP_COUNT, &next_index,
			false, NULL);
		CHECK(selected != NULL);
		CHECK(strcmp(selected->mapname,
			configured_names[transition % MAP_COUNT]) == 0);
	}
	CHECK(next_index == 1);

	next_index = -1;
	startup_map = NULL;
	CHECK(MapList_SequentialStartup(maps, MAP_COUNT, "not-in-list",
		&next_index, &startup_map));
	CHECK(startup_map == &maps[0]);
	CHECK(next_index == 1);
}

static void test_single_entry_wrap(void)
{
	MapInfo map = {"onlymap", 0, 99, NULL};
	const MapInfo *startup_map = NULL;
	const MapInfo *selected;
	int next_index = -1;

	CHECK(MapList_SequentialStartup(&map, 1, "onlymap", &next_index,
		&startup_map));
	CHECK(startup_map == NULL);
	CHECK(next_index == 0);
	selected = MapList_SelectNext(&map, 1, &next_index, false, NULL);
	CHECK(selected == &map);
	CHECK(next_index == 0);
}

static void test_random_isolation(void)
{
	MapInfo sequential[3] = {
		{"zeta", 0, 99, NULL},
		{"alpha", 0, 99, NULL},
		{"middle", 0, 99, NULL}
	};
	MapInfo random_maps[3];
	const MapInfo *selected;
	int next_index = 2;

	memcpy(random_maps, sequential, sizeof(random_maps));
	MapList_Configure(sequential, 3, false);
	CHECK(strcmp(sequential[0].mapname, "zeta") == 0);
	CHECK(strcmp(sequential[1].mapname, "alpha") == 0);
	CHECK(strcmp(sequential[2].mapname, "middle") == 0);

	MapList_Configure(random_maps, 3, true);
	CHECK(strcmp(random_maps[0].mapname, "alpha") == 0);
	CHECK(strcmp(random_maps[1].mapname, "middle") == 0);
	CHECK(strcmp(random_maps[2].mapname, "zeta") == 0);

	selected = MapList_SelectNext(random_maps, 3, &next_index, true,
		&random_maps[1]);
	CHECK(selected == &random_maps[1]);
	CHECK(next_index == 2);

	selected = MapList_SelectNext(sequential, 3, &next_index, false, NULL);
	CHECK(selected == &sequential[2]);
	CHECK(next_index == 0);
}

int main(void)
{
	test_startup_order_and_wrap();
	test_single_entry_wrap();
	test_random_isolation();

	if (failures)
	{
		fprintf(stderr, "maplist rotation tests: %d failure(s)\n", failures);
		return 1;
	}

	puts("maplist rotation tests: ok");
	return 0;
}
