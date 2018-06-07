#include <stdio.h>
#include <errno.h>

#include "levenshtein.h"
#include <stdlib.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>	/* for isatty() */
#include "table.h"
#include "misc.h"

#define ERR(fmt, ...) \
	do { \
		if (trm) \
			printf("%s%s", colors[CRED], colors[CBLD]); \
		printf("error: "); \
		if (trm) \
			printf("%s", colors[CNRM]); \
		printf(fmt, ##__VA_ARGS__); \
	} while (0)

#define INF(fmt, ...) \
	do { \
		if (args.verbose_set) \
			printf(fmt, ##__VA_ARGS__); \
	} while (0)

/*
 * True if STDOUT is a terminal
 */
static int trm;

struct cmd {
	const char *cmd;
	const char *short_d;
	const char *long_d;
	int (*func)(struct cmd *cmd);
	int (*parse_args)(int argc, char **args, int i);
	int (*help)(struct cmd *cmd);
	struct cmd *subcommands;
	struct cmd *parent;
	int dist;
};

struct cmd *find_cmd(char *cmd, const struct cmd *cmds)
{
	do {
		if (!strcmp(cmd, (*cmds).cmd))
			return (struct cmd *)cmds;
	} while ((*++cmds).cmd);

	return NULL;
}

void print_usage(const struct cmd *cmd)
{
	struct cmd *cmds = cmd->subcommands;
	char s_cmd[10][256];
	int i = 0;
	
	do {
		sprintf(s_cmd[i++], "%s", cmd->cmd);
	} while ((cmd = cmd->parent));

	printf("Usage: ");

	while (--i >= 0)
		printf("%s%s%s ", CLR(trm, CBLD, s_cmd[i]));

	printf("{");

	clr_print(trm, CBLD, "%s", (*cmds).cmd);

	while ((*++cmds).cmd)
		printf("|%s%s%s", CLR(trm, CBLD, (*cmds).cmd));

	printf("} [ARGUMENTS]\n");
}

int help_cmd(struct cmd *cmd)
{
	struct cmd *cmds = cmd->subcommands;

	print_usage(cmd);

	printf("\n%s\n", cmd->long_d);

	if (!cmds)
		return 0;

	printf("\nSubcommands:\n");
	do
		printf("     %-*s%s\n", 10, (*cmds).cmd, (*cmds).long_d);
	while ((*++cmds).cmd);
}

static struct cmd cmds[];

static struct cmd clt_cmds[] = {
	{"session", "session related commands",
		"Execute session related commands.", NULL, NULL, help_cmd, NULL, cmds},
	{"path", "path related commands",
		"Execute path related commands.", NULL, NULL, help_cmd, NULL, cmds},
	{"dev", "device related commands",
		"Execute device related commands.", NULL, NULL, help_cmd, NULL, cmds},

	{ 0 }
};

static struct cmd srv_cmds[] = {
	{"sess", "session related commands",
		"Execute session related commands.", NULL, NULL, help_cmd, NULL, cmds},
	{"path", "path related commands",
		"Execute path related commands.", NULL, NULL, help_cmd, NULL, cmds},
	{"dev", "device related commands",
		"Execute device related commands.", NULL, NULL, help_cmd, NULL, cmds},

	{ 0 }
};

static struct cmd ibnbd;

static struct cmd cmds[] = {
	{"clt", "client commands",
		"Execute client related commands.", NULL, NULL, help_cmd, clt_cmds, &ibnbd},
	{"srv", "server commands",
		"Execute server related commands.", NULL, NULL, help_cmd, srv_cmds, &ibnbd},
	{"help", "print help",
		"Print help message and exit.", NULL, NULL, help_cmd, NULL, &ibnbd},
	{ 0 }
};

static struct cmd ibnbd =
	{"ibnbd", "ibnbd", "Administration utility for the IBNBD driver.",
	NULL, NULL, help_cmd, cmds, NULL};

int main(int argc, char **argv)
{
	struct cmd *cmd;
	int ret = 0, i = 1;

	trm = (isatty(STDOUT_FILENO) == 1);

	if (argc < 2) {
		ERR("no command specified\n");
		help_cmd(&ibnbd);
		ret = -EINVAL;
		goto out;
	}

	cmd = find_cmd(argv[i], cmds);
	if (!cmd) {
		printf("'%s' is not a valid command. Try '%s%s%s %s%s%s'\n",
		       argv[i], CLR(trm, CBLD, argv[0]),
		       CLR(trm, CBLD, "help"));
		//handle_unknown_cmd(argv[i], cmds);
		ret = -EINVAL;
		goto out;
	}

	if (!strcmp(cmd->cmd, "help")) {
		help_cmd(&ibnbd);
		return 0;
	}

	if (i + 1 < argc && cmd->help &&
	     (!strcmp(argv[i + 1], "help") ||
	      !strcmp(argv[i + 1], "--help") ||
	      !strcmp(argv[i + 1], "-h") ||
	      !strcmp(cmd->cmd, "help"))) {
		cmd->help(cmd);
		goto out;
	}

	return ret;
out:
	return ret;
}
