#include "g_local.h"
#include "g_ctffunc.h"          /* CTF_TEAM_RED/BLUE for `sv sg add red|blue` */
#include "slipgate/sg_local.h"  /* the SLIPGATE admin surface behind `sv sg` */
#include "slipgate/sg_compound_swim_game.h"
#include "slipgate/sg_push_game.h"
#include "slipgate/sg_rocketjump_game.h"
#include "slipgate/sg_compound_drop_game.h"
#include "slipgate/sg_compound_hook_game.h"
#include "ctf_file_io.h"

void SpawnLoadout_ListItems(void);

void ctf_BSafePrint(long print_priority, char * buf);


void	Svcmd_Test_f (void)
{
	gi.cprintf (NULL, PRINT_HIGH, "Svcmd_Test_f()\n");
}

void	Svcmd_NextLevel_f (void)
{
	EndDMLevel();
	gi.dprintf ("Skipping to next level\n");
}


/*
==============================================================================

PACKET FILTERING
 

You can add or remove addresses from the filter list with:

addip <ip>
removeip <ip>

The ip address is specified in dot format, and any unspecified digits will match any value, so you can specify an entire class C network with "addip 192.246.40".

Removeip will only remove an address specified exactly the same way.  You cannot addip a subnet, then removeip a single host.

listip
Prints the current list of filters.

writeip
Dumps "addip <ip>" commands to listip.cfg so it can be execed at a later date.  The filter lists are not saved and restored by default, because I beleive it would cause too much confusion.

filterban <0 or 1>

If 1 (the default), then ip addresses matching the current list will be prohibited from entering the game.  This is the default setting.

If 0, then only addresses matching the list will be allowed.  This lets you easily set up a private game, or a game that only allows players from your local network.


==============================================================================
*/

typedef struct
{
	unsigned	mask;
	unsigned	compare;
} ipfilter_t;

#define	MAX_IPFILTERS	1024

ipfilter_t	ipfilters[MAX_IPFILTERS];
int			numipfilters;

/*
=================
StringToFilter
=================
*/
static qboolean StringToFilter (char *s, ipfilter_t *f)
{
	char	num[128];
	int		i, j;
	byte	b[4];
	byte	m[4];
	
	for (i=0 ; i<4 ; i++)
	{
		b[i] = 0;
		m[i] = 0;
	}
	
	for (i=0 ; i<4 ; i++)
	{
		if (*s < '0' || *s > '9')
		{
			gi.cprintf(NULL, PRINT_HIGH, "Bad filter address: %s\n", s);
			return false;
		}
		
		j = 0;
		while (*s >= '0' && *s <= '9')
		{
			num[j++] = *s++;
		}
		num[j] = 0;
		b[i] = atoi(num);
		if (b[i] != 0)
			m[i] = 255;

		if (!*s)
			break;
		s++;
	}
	
	memcpy(&f->mask, m, sizeof f->mask);
	memcpy(&f->compare, b, sizeof f->compare);

	return true;
}

/*
=================
SV_FilterPacket
=================
*/
qboolean SV_FilterPacket (char *from)
{
	int		i;
	unsigned	in;
	byte m[4] = { 0 };
	char *p;

	i = 0;
	p = from;
	while (*p && i < 4) {
		m[i] = 0;
		while (*p >= '0' && *p <= '9') {
			m[i] = m[i]*10 + (*p - '0');
			p++;
		}
		if (!*p || *p == ':')
			break;
		i++, p++;
	}
	
	memcpy(&in, m, sizeof in);

	for (i=0 ; i<numipfilters ; i++)
		if ( (in & ipfilters[i].mask) == ipfilters[i].compare)
			return (int)filterban->value;

	return (int)!filterban->value;
}


/*
=================
SV_AddIP_f
=================
*/
void SVCmd_AddIP_f (void)
{
	int		i;
	
	if (gi.argc() < 3) {
		gi.cprintf(NULL, PRINT_HIGH, "Usage:  addip <ip-mask>\n");
		return;
	}

	for (i=0 ; i<numipfilters ; i++)
		if (ipfilters[i].compare == 0xffffffff)
			break;		// free spot
	if (i == numipfilters)
	{
		if (numipfilters == MAX_IPFILTERS)
		{
			gi.cprintf (NULL, PRINT_HIGH, "IP filter list is full\n");
			return;
		}
		numipfilters++;
	}
	
	if (!StringToFilter (gi.argv(2), &ipfilters[i]))
		ipfilters[i].compare = 0xffffffff;
}

/*
=================
SV_RemoveIP_f
=================
*/
void SVCmd_RemoveIP_f (void)
{
	ipfilter_t	f;
	int			i, j;

	if (gi.argc() < 3) {
		gi.cprintf(NULL, PRINT_HIGH, "Usage:  sv removeip <ip-mask>\n");
		return;
	}

	if (!StringToFilter (gi.argv(2), &f))
		return;

	for (i=0 ; i<numipfilters ; i++)
		if (ipfilters[i].mask == f.mask
		&& ipfilters[i].compare == f.compare)
		{
			for (j=i+1 ; j<numipfilters ; j++)
				ipfilters[j-1] = ipfilters[j];
			numipfilters--;
			gi.cprintf (NULL, PRINT_HIGH, "Removed.\n");
			return;
		}
	gi.cprintf (NULL, PRINT_HIGH, "Didn't find %s.\n", gi.argv(2));
}

/*
=================
SV_ListIP_f
=================
*/
void SVCmd_ListIP_f(void)
{
	int		i;
	size_t		j;
	byte	b[4];

	gi.cprintf(NULL, PRINT_HIGH, "Filter list:\n");
	for (i = 0; i < numipfilters; i++)
	{
		for (j = 0; j < sizeof b; j++)
		{
			b[j] = (ipfilters[i].compare >> (j * 8)) & 0xff;
		}
		gi.cprintf(NULL, PRINT_HIGH, "%3i.%3i.%3i.%3i\n", b[0], b[1], b[2], b[3]);
	}
}

/*
=================
SV_WriteIP_f
=================
*/
void SVCmd_WriteIP_f (void)
{
	FILE* f;
	char	name[MAX_OSPATH];
	byte	b[4];
	int		i;
	size_t		j;

	if (gamedir->string && gamedir->string[0])
		sprintf(name, "./%s/listip.cfg", gamedir->string);
	else
		sprintf(name, "./listip.cfg");

	gi.cprintf(NULL, PRINT_HIGH, "Writing %s.\n", name);

	f = fopen(name, "wb");
	if (!f)
	{
		gi.cprintf(NULL, PRINT_HIGH, "Couldn't open %s\n", name);
		return;
	}

	fprintf(f, "set filterban %d\n", (int)filterban->value);

	for (i = 0; i < numipfilters; i++)
	{
		for (j = 0; j < sizeof b; j++)
		{
			b[j] = (ipfilters[i].compare >> (j * 8)) & 0xff;
		}
		fprintf(f, "sv addip %u.%u.%u.%u\n", b[0], b[1], b[2], b[3]);
	}

	fclose(f);
}

void SVCmd_QuadTime_f(void)
{
        unsigned long i=0;
        gitem_t * target = NULL;

        if (gi.argc() < 3) {
                gi.cprintf(NULL, PRINT_HIGH, "Usage:  sv quadtime <seconds>\n");
                return;
        }

        if (!sscanf(gi.argv(2), "%lu", &i))
        {
	        gi.cprintf(NULL, PRINT_HIGH, "Usage: sv quadtime <seconds>\n");

                return;
        }

        target = FindItem("Quad Damage");
        if (target && i > 0 && i < 1200) {
                target->quantity = i;
		char buffer[MAX_INFO_STRING];
		sprintf(buffer, "Quad respawn updated to %lu\n", i);
		ctf_BSafePrint(PRINT_HIGH, buffer);
        }
}

/* Dispatch SLIPGATE roster commands. Bare `sv sg remove` removes all bots. */
static void SVCmd_SG_f (void)
{
	char *sub = gi.argv(2);
	char *arg = gi.argv(3);
	if (Q_stricmp(sub, "add") == 0)
	{
		int team = 0;   /* 0: let the balancer place it, as it always has */

		if (Q_stricmp(arg, "red") == 0)
			team = CTF_TEAM_RED;
		else if (Q_stricmp(arg, "blue") == 0)
			team = CTF_TEAM_BLUE;
		else if (*arg)
		{
			gi.cprintf(NULL, PRINT_HIGH, "usage: sv sg add [red|blue]\n");
			return;
		}
		if (!SG_AddBotTeam(team))
			gi.cprintf(NULL, PRINT_HIGH, "slipgate: could not add bot\n");
	}
	else if (Q_stricmp(sub, "remove") == 0)
	{
		if (!*arg)
			gi.cprintf(NULL, PRINT_HIGH, "slipgate: removed %d\n",
			           SG_RemoveBots());
		else if (!SG_RemoveBotNamed(arg))
			gi.cprintf(NULL, PRINT_HIGH, "slipgate: no bot \"%s\"\n", arg);
	}
	else if (Q_stricmp(sub, "kick") == 0)
	{
		if (Q_stricmp(arg, "worst") != 0)
			gi.cprintf(NULL, PRINT_HIGH, "usage: sv sg kick worst\n");
		else if (!SG_KickWorst())
			gi.cprintf(NULL, PRINT_HIGH, "slipgate: no bots\n");
	}
	else if (Q_stricmp(sub, "list") == 0)
		SG_ListBots();
	else if (Q_stricmp(sub, "weights") == 0)
	{
		if (Q_stricmp(arg, "reload") == 0)
			SG_WeightsReload();
		SG_WeightsPrint();
	}
	else if (Q_stricmp(sub, "compoundswim") == 0)
	{
		char *end = NULL;
		long link = strtol(arg, &end, 10);

		if (!*arg || !end || *end || link < 0 || link > INT_MAX ||
		    !SG_CompoundSwimGameStageAuthenticatedProbe((int)link))
			gi.cprintf(NULL, PRINT_HIGH,
			    "slipgate: authenticated compound swim probe refused\n");
	}
	else if (Q_stricmp(sub, "rocketjump") == 0)
	{
		char *end = NULL;
		long link = strtol(arg, &end, 10);

		if (!*arg || !end || *end || link < 0 || link > INT_MAX ||
		    !SG_RocketJumpGameStageAuthenticatedProbe((int)link))
			gi.cprintf(NULL, PRINT_HIGH,
			    "slipgate: authenticated rocketjump probe refused\n");
	}
	else if (Q_stricmp(sub, "push") == 0)
	{
		char *end = NULL;
		long link = strtol(arg, &end, 10);

		if (!*arg || !end || *end || link < 0 || link > INT_MAX ||
		    !SG_PushGameStageAuthenticatedProbe((int)link))
			gi.cprintf(NULL, PRINT_HIGH,
			    "slipgate: authenticated push probe refused\n");
	}
	else if (Q_stricmp(sub, "compounddrop") == 0)
	{
		char *end = NULL;
		long link = strtol(arg, &end, 10);

		if (!*arg || !end || *end || link < 0 || link > INT_MAX ||
		    !SG_CompoundDropGameStageAuthenticatedProbe((int)link))
			gi.cprintf(NULL, PRINT_HIGH,
			    "slipgate: authenticated compound drop probe refused\n");
	}
	else if (Q_stricmp(sub, "compoundhook") == 0)
	{
		char *end = NULL;
		long link = strtol(arg, &end, 10);

		if (!*arg || !end || *end || link < 0 || link > INT_MAX ||
		    !SG_CompoundHookGameStageAuthenticatedProbe((int)link))
			gi.cprintf(NULL, PRINT_HIGH,
			    "slipgate: authenticated compound hook probe refused\n");
	}
	else
		gi.cprintf(NULL, PRINT_HIGH,
		           "usage: sv sg <add [red|blue] | list | remove [name|slot] "
		           "| kick worst | weights [reload] | compoundswim <link> "
		           "| rocketjump <link> | push <link> | compounddrop <link> "
		           "| compoundhook <link>>\n");
}

static void SVCmd_POVRecord_f(void)
{
	qboolean stop;
	const char *spectator;
	const char *target;

	if (gi.argc() != 4)
	{
		gi.cprintf(NULL, PRINT_HIGH,
			"usage: sv povrecord <spectator> <target> | "
			"sv povrecord off <spectator>\n");
		return;
	}
	stop = strcmp(gi.argv(2), "off") == 0;
	spectator = stop ? gi.argv(3) : gi.argv(2);
	target = stop ? NULL : gi.argv(3);
	if (!POVRecord_AdminDirective(spectator, target, stop))
		gi.cprintf(NULL, PRINT_HIGH, "povrecord: directive rejected\n");
}

/*
=================
ServerCommand

ServerCommand will be called when an "sv" command is issued.
The game can issue gi.argc() / gi.argv() commands to get the rest
of the parameters
=================
*/
void	ServerCommand (void)
{
	// BUZZKILL - spawn_loadout discovery: every addressable token, live
	if (Q_stricmp(gi.argv(1), "listitems") == 0)
	{
		SpawnLoadout_ListItems();
		return;
	}

	char	*cmd;

	cmd = gi.argv(1);
	if (Q_stricmp (cmd, "test") == 0)
		Svcmd_Test_f ();
	else if (Q_stricmp (cmd, "addip") == 0)
		SVCmd_AddIP_f ();
	else if (Q_stricmp (cmd, "removeip") == 0)
		SVCmd_RemoveIP_f ();
	else if (Q_stricmp (cmd, "listip") == 0)
		SVCmd_ListIP_f ();
	else if (Q_stricmp (cmd, "writeip") == 0)
		SVCmd_WriteIP_f ();
	else if (Q_stricmp (cmd, "quadtime") == 0)
		SVCmd_QuadTime_f ();
	else if (Q_stricmp (cmd, "statsdb") == 0)
		CTF_StatsDB_Command ();
	else if ((Q_stricmp (cmd, "next") == 0) || (Q_stricmp (cmd, "skip") == 0))
		Svcmd_NextLevel_f ();
	else if (Q_stricmp (cmd, "sg") == 0)
		SVCmd_SG_f ();
	else if (Q_stricmp (cmd, "povrecord") == 0)
		SVCmd_POVRecord_f ();
	else if (Q_stricmp (cmd, "rune") == 0)
	{
		/* SLIPGATE: generation remains the default; human evidence is an
		 * explicit, source-bound update transaction. */
		if (gi.argc() == 2)
			Rune_Generate(level.mapname);
		else if (gi.argc() == 3 &&
		         Q_stricmp(gi.argv(2), "update") == 0)
			Rune_Update(level.mapname);
		else
			gi.cprintf(NULL, PRINT_HIGH, "usage: sv rune [update]\n");
	}
	else
		gi.cprintf (NULL, PRINT_HIGH, "Unknown server command \"%s\"\n", cmd);
}
