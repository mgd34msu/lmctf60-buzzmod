/* g_entfile_path.h -- bounded construction for game-side .ent overrides. */
#ifndef G_ENTFILE_PATH_H
#define G_ENTFILE_PATH_H

#include <stddef.h>
#include <stdio.h>

static inline int G_EntFilePath(char *path, size_t path_size,
	const char *gamedir_name, const char *mapname)
{
	int written;

	if (path && path_size > 0)
		path[0] = '\0';
	if (!path || path_size == 0 || !gamedir_name || !mapname)
		return 0;
	written = snprintf(path, path_size, "%s/ent/%s.ent",
	                   gamedir_name, mapname);
	if (written < 0 || (size_t)written >= path_size)
	{
		path[0] = '\0';
		return 0;
	}
	return 1;
}

#endif
