// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>	/* for isatty() */
#include <stdbool.h>

#include "levenshtein.h"
#include "table.h"
#include "misc.h"
#include "list.h"

#include "ibnbd-sysfs.h"
#include "ibnbd-clms.h"

#define INF(verbose_set, fmt, ...)		\
	do { \
		if (verbose_set) \
			printf(fmt, ##__VA_ARGS__); \
	} while (0)

static struct ibnbd_sess_dev **sds_clt;
static struct ibnbd_sess_dev **sds_srv;
static struct ibnbd_sess **sess_clt;
static struct ibnbd_sess **sess_srv;
static struct ibnbd_path **paths_clt;
static struct ibnbd_path **paths_srv;
static int sds_clt_cnt, sds_srv_cnt,
	   sess_clt_cnt, sess_srv_cnt,
	   paths_clt_cnt, paths_srv_cnt;

static struct ibnbd_ctx ctx;

struct sarg {
	enum ibnbd_token tok;
	const char *str;
	const char *descr;
	int (*parse)(int argc, const char *argv[], int i,
		     const struct sarg *sarg, struct ibnbd_ctx *ctx);
	size_t offset;
	int dist;
};

static int parse_fmt(int argc, const char *argv[], int i,
		     const struct sarg *sarg, struct ibnbd_ctx *ctx)
{
	if (!strcasecmp(argv[i], "csv"))
		ctx->fmt = FMT_CSV;
	else if (!strcasecmp(argv[i], "json"))
		ctx->fmt = FMT_JSON;
	else if (!strcasecmp(argv[i], "xml"))
		ctx->fmt = FMT_XML;
	else if (!strcasecmp(argv[i], "term"))
		ctx->fmt = FMT_TERM;
	else
		return i;

	ctx->fmt_set = true;

	return i + 1;
}

static int parse_io_mode(int argc, const char *argv[], int i,
			 const struct sarg *sarg, struct ibnbd_ctx *ctx)
{
	if (strcasecmp(argv[i], "blockio") &&
	    strcasecmp(argv[i], "fileio"))
		return i;

	strcpy(ctx->io_mode, argv[i]);

	ctx->io_mode_set = true;

	return i + 1;
}

enum lstmode {
	LST_DEVICES,
	LST_SESSIONS,
	LST_PATHS
};

static int parse_lst(int argc, const char *argv[], int i,
		     const struct sarg *sarg, struct ibnbd_ctx *ctx)
{
	if (!strcasecmp(argv[i], "devices") ||
	    !strcasecmp(argv[i], "device") ||
	    !strcasecmp(argv[i], "devs") ||
	    !strcasecmp(argv[i], "dev"))
		ctx->lstmode = LST_DEVICES;
	else if (!strcasecmp(argv[i], "sessions") ||
		 !strcasecmp(argv[i], "session") ||
		 !strcasecmp(argv[i], "sess"))
		ctx->lstmode = LST_SESSIONS;
	else if (!strcasecmp(argv[i], "paths") ||
		 !strcasecmp(argv[i], "path"))
		ctx->lstmode = LST_PATHS;
	else
		return i;

	ctx->lstmode_set = true;

	return i + 1;
}

enum ibnbdmode {
	IBNBD_NONE = 0,
	IBNBD_CLIENT = 1,
	IBNBD_SERVER = 1 << 1,
	IBNBD_BOTH = IBNBD_CLIENT | IBNBD_SERVER,
};

static int parse_from(int argc, const char *argv[], int i,
		      const struct sarg *sarg, struct ibnbd_ctx *ctx)
{
	int j = i + 1;

	if (j >= argc) {
		ERR(ctx->trm, "Please specify the destination to map from\n");
		return i;
	}

	ctx->from = argv[j];
	ctx->from_set = 1;

	return j + 1;
}

static int parse_help(int argc, const char *argv[], int i,
		      const struct sarg *sarg, struct ibnbd_ctx *ctx)
{
	int j = i + 1;

	ctx->help_set = true;

	if (j < argc) {

		ctx->help_arg = argv[j];
		ctx->help_arg_set = 1;
	}
	return j + 1;
}

static int parse_argv0(const char *argv0, struct ibnbd_ctx *ctx)
{
	const char *prog_name = strrchr(argv0, '/');

	if (!prog_name)
		prog_name = argv0;
	else
		prog_name++;

	ctx->pname = prog_name;

	if (!strcasecmp(prog_name, "ibnbd-clt2")
	    || !strcasecmp(prog_name, "ibnbd-clt"))
		ctx->ibnbdmode = IBNBD_CLIENT;
	else if (!strcasecmp(prog_name, "ibnbd-srv2")
		 || !strcasecmp(prog_name, "ibnbd-srv"))
		ctx->ibnbdmode = IBNBD_SERVER;
	else
		return 0;

	ctx->ibnbdmode_set = true;

	return 1;
}

static int parse_mode(int argc, const char *argv[], int i,
		     const struct sarg *sarg, struct ibnbd_ctx *ctx)
{
	if (!strcasecmp(argv[i], "client") || !strcasecmp(argv[i], "clt"))
		ctx->ibnbdmode = IBNBD_CLIENT;
	else if (!strcasecmp(argv[i], "server") || !strcasecmp(argv[i], "srv"))
		ctx->ibnbdmode = IBNBD_SERVER;
	else if (!strcasecmp(argv[i], "both"))
		ctx->ibnbdmode = IBNBD_BOTH;
	else
		return i;

	ctx->ibnbdmode_set = true;

	return i + 1;
}

static int parse_rw(int argc, const char *argv[], int i,
		     const struct sarg *sarg, struct ibnbd_ctx *ctx)
{
	if (strcasecmp(argv[i], "ro") &&
	    strcasecmp(argv[i], "rw") &&
	    strcasecmp(argv[i], "migration"))
		return i;

	ctx->access_mode = argv[i];
	ctx->access_mode_set = true;

	return i + 1;
}

static int clm_set_hdr_unit(struct table_column *clm, char const *unit)
{
	size_t len;

	len = strlen(clm->m_header);

	clm->m_width = len + snprintf(clm->m_header + len,
				      sizeof(clm->m_header) - len, " (%s)",
				      unit);

	return 0;
}

static int parse_unit(int argc, const char *argv[], int i,
		     const struct sarg *sarg, struct ibnbd_ctx *ctx)
{
	int rc;

	rc = get_unit_index(sarg->str, &ctx->unit_id);
	if (rc < 0)
		return i;

	clm_set_hdr_unit(&clm_ibnbd_dev_rx_sect, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_dev_tx_sect, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_sess_rx_bytes, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_sess_tx_bytes, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_path_rx_bytes, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_path_tx_bytes, sarg->descr);

	ctx->unit_set = true;
	return i + 1;
}

static int parse_all(int argc, const char *argv[], int i,
		     const struct sarg *sarg, struct ibnbd_ctx *ctx)
{
	memcpy(&ctx->clms_devices_clt, &all_clms_devices_clt,
	       ARRSIZE(all_clms_devices_clt) * sizeof(all_clms_devices[0]));
	memcpy(&ctx->clms_devices_srv, &all_clms_devices_srv,
	       ARRSIZE(all_clms_devices_srv) * sizeof(all_clms_devices[0]));
	memcpy(&ctx->clms_sessions_clt, &all_clms_sessions_clt,
	       ARRSIZE(all_clms_sessions_clt) * sizeof(all_clms_sessions[0]));
	memcpy(&ctx->clms_sessions_srv, &all_clms_sessions_srv,
	       ARRSIZE(all_clms_sessions_srv) * sizeof(all_clms_sessions[0]));
	memcpy(&ctx->clms_paths_clt, &all_clms_paths_clt,
	       ARRSIZE(all_clms_paths_clt) * sizeof(all_clms_paths[0]));
	memcpy(&ctx->clms_paths_srv, &all_clms_paths_srv,
	       ARRSIZE(all_clms_paths_srv) * sizeof(all_clms_paths[0]));

	return i + 1;
}

static int parse_flag(int argc, const char *argv[], int i,
		     const struct sarg *sarg, struct ibnbd_ctx *ctx)
{
	*(short *)(((char *)ctx)+(sarg->offset)) = 1;

	return i + 1;
}

static struct sarg _sargs_from =
	{TOK_FROM, "from", "Destination to map a device from", parse_from, 0};
static struct sarg _sargs_client =
	{TOK_CLIENT, "client", "Information for client", parse_mode, 0};
static struct sarg _sargs_clt =
	{TOK_CLIENT, "clt", "Information for client", parse_mode, 0};
static struct sarg _sargs_server =
	{TOK_SERVER, "server", "Information for server", parse_mode, 0};
static struct sarg _sargs_srv =
	{TOK_SERVER, "srv", "Information for server", parse_mode, 0};
static struct sarg _sargs_both =
	{TOK_BOTH, "both", "Information for both", parse_mode, 0};
static struct sarg _sargs_devices_client =
	{TOK_DEVICES, "devices", "Map/unmapped/modify devices", parse_lst, 0};
static struct sarg _sargs_devices =
	{TOK_DEVICES, "devices", "List devices", parse_lst, 0};
static struct sarg _sargs_device =
	{TOK_DEVICES, "device", "", parse_lst, 0};
static struct sarg _sargs_devs =
	{TOK_DEVICES, "devs", "", parse_lst, 0};
static struct sarg _sargs_dev =
	{TOK_DEVICES, "dev", "", parse_lst, 0};
static struct sarg _sargs_sessions =
	{TOK_SESSIONS, "sessions", "Operate on sessions", parse_lst, 0};
static struct sarg _sargs_session =
	{TOK_SESSIONS, "session", "", parse_lst, 0};
static struct sarg _sargs_sess =
	{TOK_SESSIONS, "sess", "", parse_lst, 0};
static struct sarg _sargs_paths =
	{TOK_PATHS, "paths", "Handle paths", parse_lst, 0};
static struct sarg _sargs_path =
	{TOK_PATHS, "path", "", parse_lst, 0};
static struct sarg _sargs_notree =
	{TOK_NOTREE, "notree", "Don't display paths for each sessions",
	 parse_flag, offsetof(struct ibnbd_ctx, notree_set)};
static struct sarg _sargs_xml =
	{TOK_XML, "xml", "Print in XML format", parse_fmt, 0};
static struct sarg _sargs_cvs =
	{TOK_CSV, "csv", "Print in CSV format", parse_fmt, 0};
static struct sarg _sargs_json =
	{TOK_JSON, "json", "Print in JSON format", parse_fmt, 0};
static struct sarg _sargs_term =
	{TOK_TERM, "term", "Print for terminal", parse_fmt, 0};
static struct sarg _sargs_ro =
	{TOK_RO, "ro", "Readonly", parse_rw, 0};
static struct sarg _sargs_rw =
	{TOK_RW, "rw", "Writable", parse_rw, 0};
static struct sarg _sargs_migration =
	{TOK_MIGRATION, "migration", "Writable (migration)", parse_rw, 0};
static struct sarg _sargs_blockio =
	{TOK_BLOCKIO, "blockio", "Block IO mode", parse_io_mode, 0};
static struct sarg _sargs_fileio =
	{TOK_FILEIO, "fileio", "File IO mode", parse_io_mode, 0};
static struct sarg _sargs_help =
	{TOK_HELP, "help", "Display help and exit", parse_help,
	 offsetof(struct ibnbd_ctx, help_set)};
static struct sarg _sargs_verbose =
	{TOK_VERBOSE, "verbose", "Verbose output", parse_flag,
	 offsetof(struct ibnbd_ctx, verbose_set)};
static struct sarg _sargs_minus_v =
	{TOK_VERBOSE, "-v", "Verbose output", parse_flag,
	 offsetof(struct ibnbd_ctx, verbose_set)};
static struct sarg _sargs_byte =
	{TOK_BYTE, "B", "Byte", parse_unit, 0};
static struct sarg _sargs_kib =
	{TOK_KIB, "K", "KiB", parse_unit, 0};
static struct sarg _sargs_mib =
	{TOK_MIB, "M", "MiB", parse_unit, 0};
static struct sarg _sargs_gib =
	{TOK_GIB, "G", "GiB", parse_unit, 0};
static struct sarg _sargs_tib =
	{TOK_TIB, "T", "TiB", parse_unit, 0};
static struct sarg _sargs_pib =
	{TOK_PIB, "P", "PiB", parse_unit, 0};
static struct sarg _sargs_eib =
	{TOK_EIB, "E", "EiB", parse_unit, 0};
static struct sarg _sargs_noheaders =
	{TOK_NOHEADERS, "noheaders", "Don't print headers", parse_flag,
	 offsetof(struct ibnbd_ctx, noheaders_set)};
static struct sarg _sargs_nototals =
	{TOK_NOTOTALS, "nototals", "Don't print totals", parse_flag,
	 offsetof(struct ibnbd_ctx, nototals_set)};
static struct sarg _sargs_force =
	{TOK_FORCE, "force", "Force operation", parse_flag,
	 offsetof(struct ibnbd_ctx, force_set)};
static struct sarg _sargs_noterm =
	{TOK_NOTERM, "noterm", "Non-interactive mode", parse_flag,
	 offsetof(struct ibnbd_ctx, noterm_set)};
static struct sarg _sargs_minus_f =
	{TOK_FORCE, "-f", "", parse_flag,
	 offsetof(struct ibnbd_ctx, force_set)};
static struct sarg _sargs_all =
	{TOK_ALL, "all", "Print all columns", parse_all, 0};
static struct sarg _sargs_null =
	{TOK_NONE, 0};

static struct sarg *sargs[] = {
	&_sargs_from,
	&_sargs_client,
	&_sargs_clt,
	&_sargs_server,
	&_sargs_srv,
	&_sargs_both,
	&_sargs_devices,
	&_sargs_device,
	&_sargs_devs,
	&_sargs_dev,
	&_sargs_sessions,
	&_sargs_session,
	&_sargs_sess,
	&_sargs_paths,
	&_sargs_path,
	&_sargs_notree,
	&_sargs_xml,
	&_sargs_cvs,
	&_sargs_json,
	&_sargs_term,
	&_sargs_ro,
	&_sargs_rw,
	&_sargs_migration,
	&_sargs_blockio,
	&_sargs_fileio,
	&_sargs_help,
	&_sargs_verbose,
	&_sargs_minus_v,
	&_sargs_byte,
	&_sargs_kib,
	&_sargs_mib,
	&_sargs_gib,
	&_sargs_tib,
	&_sargs_pib,
	&_sargs_eib,
	&_sargs_noheaders,
	&_sargs_nototals,
	&_sargs_force,
	&_sargs_noterm,
	&_sargs_minus_f,
	&_sargs_all,
	&_sargs_null
};

static struct sarg *sargs_mode[] = {
	&_sargs_client,
	&_sargs_clt,
	&_sargs_server,
	&_sargs_srv,
	&_sargs_both,
	&_sargs_help,
	&_sargs_null
};

static struct sarg *sargs_object_type[] = {
	&_sargs_devices,
	&_sargs_device,
	&_sargs_devs,
	&_sargs_dev,
	&_sargs_sessions,
	&_sargs_session,
	&_sargs_sess,
	&_sargs_paths,
	&_sargs_path,
	&_sargs_help,
	&_sargs_null
};

static struct sarg *sargs_object_type_help_client[] = {
	&_sargs_devices_client,
	&_sargs_sessions,
	&_sargs_paths,
	&_sargs_help,
	&_sargs_null
};

static struct sarg *sargs_object_type_help_server[] = {
	&_sargs_devices,
	&_sargs_sessions,
	&_sargs_paths,
	&_sargs_help,
	&_sargs_null
};

static struct sarg *sargs_list_parameters[] = {
	&_sargs_xml,
	&_sargs_cvs,
	&_sargs_json,
	&_sargs_term,
	&_sargs_byte,
	&_sargs_kib,
	&_sargs_mib,
	&_sargs_gib,
	&_sargs_tib,
	&_sargs_pib,
	&_sargs_eib,
	&_sargs_notree,
	&_sargs_noheaders,
	&_sargs_nototals,
	&_sargs_noterm,
	&_sargs_all,
	&_sargs_verbose,
	&_sargs_help,
	&_sargs_null
};

static const struct sarg *find_sarg(const char *str,
				    struct sarg *const sargs[])
{
	do {
		if (!strcasecmp(str, (*sargs)->str))
			return *sargs;
	} while ((*++sargs)->str);

	return NULL;
}

static  void usage_sarg(const char *str, struct sarg *const sargs[],
			const struct ibnbd_ctx *ctx)
{
	printf("Usage: %s%s%s ", CLR(ctx->trm, CBLD, str));

	clr_print(ctx->trm, CBLD, "%s", (*sargs)->str);

	while ((*++sargs)->str)
		printf("|%s%s%s", CLR(ctx->trm, CBLD, (*sargs)->str));

	printf(" ...\n\n");
}

#define HP "    "
#define HPRE HP "                "
#define HOPT HP "%-16s%s\n"

static void print_opt(const char *opt, const char *descr)
{
	printf(HOPT, opt, descr);
}

static void print_sarg_descr(char *str)
{
	const struct sarg *s;

	s = find_sarg(str, sargs);
	if (s)
		print_opt(s->str, s->descr);
}

static  void help_sarg(const char *str, struct sarg *const sargs[],
		       const struct ibnbd_ctx *ctx)
{
	usage_sarg(str, sargs, ctx);

	do {
		print_opt((*sargs)->str, (*sargs)->descr);
	} while ((*++sargs)->str);
}

struct cmd {
	enum ibnbd_token tok;
	const char *cmd;
	const char *short_d;
	const char *short_d2;
	const char *long_d;
	int (*func)(void);
	int (*parse_args)(int argc, const char *argv[], int i,
			  struct ibnbd_ctx *ctx);
	void (*help)(const struct cmd *cmd,
		     const struct ibnbd_ctx *ctx);
	int dist;
};

static const struct cmd *find_cmd(const char *cmd, struct cmd * const cmds[])
{
	if (cmd) {
		do {
			if (!strcmp(cmd, (*cmds)->cmd))
				return *cmds;
		} while ((*++cmds)->cmd);
	}
	return NULL;
}

static void print_usage(const char *sub_name, struct cmd * const cmds[],
			const struct ibnbd_ctx *ctx)
{
	if (sub_name)
		printf("Usage: %s%s%s %s {",
		       CLR(ctx->trm, CBLD, ctx->pname), sub_name);
	else
		printf("Usage: %s%s%s {", CLR(ctx->trm, CBLD, ctx->pname));

	clr_print(ctx->trm, CBLD, "%s", (*cmds)->cmd);

	while ((*++cmds)->cmd)
		printf("|%s%s%s", CLR(ctx->trm, CBLD, (*cmds)->cmd));

	printf("} [ARGUMENTS]\n\n");
}

static void print_help(const char *program_name, struct cmd * const cmds[],
		       const struct ibnbd_ctx *ctx)
{
	print_usage(program_name, cmds, ctx);
	printf("\nIBNBD command line utility.\n");
	printf("\nSubcommands:\n");
	do {
		if (program_name)
			printf("     %-*s%s %s%s\n", 20,
			       (*cmds)->cmd, (*cmds)->short_d,
			       program_name, (*cmds)->short_d2);
		else
			printf("     %-*s%s\n", 20, (*cmds)->cmd,
			       (*cmds)->short_d);
	} while ((*++cmds)->cmd);

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
}

static int cmd_help(void);

static void cmd_print_usage(const struct cmd *cmd, const char *a,
			    const struct ibnbd_ctx *ctx)
{
	printf("Usage: %s%s%s %s%s%s %s [OPTIONS]\n",
	       CLR(ctx->trm, CBLD, ctx->pname),
	       CLR(ctx->trm, CBLD, cmd->cmd), a);
	printf("\n%s\n", cmd->long_d);
}

static void print_clms_list(struct table_column **clms)
{
	if (*clms)
		printf("%s", (*clms)->m_name);

	while (*++clms)
		printf(",%s", (*clms)->m_name);

	printf("\n");
}

static void help_fields(void)
{
	print_opt("{fields}",
		  "Comma separated list of fields to be printed. The list can be");
	print_opt("",
		  "prefixed with '+' or '-' to add or remove fields from the ");
	print_opt("", "default selection.\n");
}

static void print_fields(struct table_column **def_clt,
			 struct table_column **def_srv,
			 struct table_column **all,
			 enum ibnbdmode mode)
{
	table_tbl_print_term(HPRE, all, ctx.trm, &ctx);
	if (mode != IBNBD_SERVER) {
		printf("\n%sDefault%s: ",
		       HPRE, mode == IBNBD_BOTH ? " client" : "");
		print_clms_list(def_clt);
	}
	if (mode != IBNBD_CLIENT) {
		printf("%sDefault%s: ",
		       HPRE, mode == IBNBD_BOTH ? " server" : "");
		print_clms_list(def_srv);
	}
	printf("\n");
}

static void help_list(const struct cmd *cmd,
		      const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "", ctx);

	printf("\nOptions:\n");
	print_opt("{mode}", "Information to print: sessions.");
	help_fields();

	printf("%s%s%s%s\n", HPRE, CLR(ctx->trm, CDIM, "Device Fields"));
	print_fields(def_clms_devices_clt, def_clms_devices_srv,
		     all_clms_devices, IBNBD_BOTH);

	printf("%s%s%s%s\n", HPRE, CLR(ctx->trm, CDIM, "Session Fields"));
	print_fields(def_clms_sessions_clt, def_clms_sessions_srv,
		     all_clms_sessions, IBNBD_BOTH);

	printf("%s%s%s%s\n", HPRE, CLR(ctx->trm, CDIM, "Path Fields"));
	print_fields(def_clms_paths_clt, def_clms_paths_srv,
		     all_clms_paths, IBNBD_BOTH);

	printf("%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_sarg_descr("notree");
	print_sarg_descr("noheaders");
	print_sarg_descr("nototals");
	print_sarg_descr("help");
}

static bool help_print_all(const struct ibnbd_ctx *ctx)
{
	if (ctx->help_arg_set && strncmp(ctx->help_arg, "all", 3) == 0)
		return true;
	else
		return false;
}

static bool help_print_fields(const struct ibnbd_ctx *ctx)
{
	if (ctx->help_arg_set && strncmp(ctx->help_arg, "fields", 4) == 0)
		return true;
	else
		return false;
}

static void help_list_devices(const struct cmd *cmd,
			      const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "devices", ctx);

	if (!help_print_fields(ctx))
		printf("\nOptions:\n");

	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(def_clms_devices_clt, def_clms_devices_srv,
			     all_clms_devices, ctx->ibnbdmode);

		if (help_print_fields(ctx))
			return;
	}
	printf("%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_sarg_descr("notree");
	print_sarg_descr("noheaders");
	print_sarg_descr("nototals");
	print_opt("help", "Display help and exit. [fields|all]");
}

static void help_list_sessions(const struct cmd *cmd,
			       const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "sessions", ctx);

	if (!help_print_fields(ctx))
		printf("\nOptions:\n");

	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(def_clms_sessions_clt, def_clms_sessions_srv,
			     all_clms_sessions, ctx->ibnbdmode);

		if (help_print_fields(ctx))
			return;
	}

	printf("%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_sarg_descr("notree");
	print_sarg_descr("noheaders");
	print_sarg_descr("nototals");
	print_opt("help", "Display help and exit. [fields|all]");
}

static void help_list_paths(const struct cmd *cmd,
			    const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "paths", ctx);

	if (!help_print_fields(ctx))
		printf("\nOptions:\n");

	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(def_clms_paths_clt, def_clms_paths_srv,
			     all_clms_paths, ctx->ibnbdmode);

		if (help_print_fields(ctx))
			return;
	}

	printf("%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_sarg_descr("notree");
	print_sarg_descr("noheaders");
	print_sarg_descr("nototals");
	print_opt("help", "Display help and exit. [fields|all]");
}

static int list_devices(struct ibnbd_sess_dev **d_clt, int d_clt_cnt,
			struct ibnbd_sess_dev **d_srv, int d_srv_cnt,
			struct ibnbd_ctx *ctx)
{
	if (!(ctx->ibnbdmode & IBNBD_CLIENT))
		d_clt_cnt = 0;
	if (!(ctx->ibnbdmode & IBNBD_SERVER))
		d_srv_cnt = 0;

	switch (ctx->fmt) {
	case FMT_CSV:
		if (d_clt_cnt && d_srv_cnt)
			printf("Imports:\n");

		if (d_clt_cnt)
			list_devices_csv(d_clt, ctx->clms_devices_clt, ctx);

		if (d_clt_cnt && d_srv_cnt)
			printf("Exports:\n");

		if (d_srv_cnt)
			list_devices_csv(d_srv, ctx->clms_devices_srv, ctx);

		break;
	case FMT_JSON:
		printf("{\n");

		if (d_clt_cnt) {
			printf("\t\"imports\": ");
			list_devices_json(d_clt, ctx->clms_devices_clt, ctx);
		}

		if (d_clt_cnt && d_srv_cnt)
			printf(",");

		printf("\n");

		if (d_srv_cnt) {
			printf("\t\"exports\": ");
			list_devices_json(d_srv, ctx->clms_devices_srv, ctx);
		}

		printf("\n}\n");

		break;
	case FMT_XML:
		if (d_clt_cnt) {
			printf("<imports>\n");
			list_devices_xml(d_clt, ctx->clms_devices_clt, ctx);
			printf("</imports>\n");
		}
		if (d_srv_cnt) {
			printf("<exports>\n");
			list_devices_xml(d_srv, ctx->clms_devices_srv, ctx);
			printf("</exports>\n");
		}

		break;
	case FMT_TERM:
	default:
		if (d_clt_cnt && d_srv_cnt && !ctx->noheaders_set)
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Imported devices"));

		if (d_clt_cnt)
			list_devices_term(d_clt, ctx->clms_devices_clt, ctx);

		if (d_clt_cnt && d_srv_cnt && !ctx->noheaders_set)
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Exported devices"));

		if (d_srv_cnt)
			list_devices_term(d_srv, ctx->clms_devices_srv, ctx);

		break;
	}

	return 0;
}

static int list_sessions(struct ibnbd_sess **s_clt, int clt_s_num,
			 struct ibnbd_sess **s_srv, int srv_s_num,
			 struct ibnbd_ctx *ctx)
{
	if (!(ctx->ibnbdmode & IBNBD_CLIENT))
		clt_s_num = 0;
	if (!(ctx->ibnbdmode & IBNBD_SERVER))
		srv_s_num = 0;

	switch (ctx->fmt) {
	case FMT_CSV:
		if (clt_s_num && srv_s_num)
			printf("Outgoing:\n");

		if (clt_s_num)
			list_sessions_csv(s_clt, ctx->clms_sessions_clt, ctx);

		if (clt_s_num && srv_s_num)
			printf("Incoming:\n");

		if (srv_s_num)
			list_sessions_csv(s_srv, ctx->clms_sessions_srv, ctx);
		break;
	case FMT_JSON:
		printf("{\n");

		if (clt_s_num) {
			printf("\t\"outgoing\": ");
			list_sessions_json(s_clt, ctx->clms_sessions_clt, ctx);
		}

		if (clt_s_num && srv_s_num)
			printf(",");

		printf("\n");

		if (srv_s_num) {
			printf("\t\"incoming\": ");
			list_sessions_json(s_srv, ctx->clms_sessions_srv, ctx);
		}

		printf("\n}\n");

		break;
	case FMT_XML:
		if (clt_s_num) {
			printf("\t\"outgoing\": ");
			printf("<outgoing>\n");
			list_sessions_xml(s_clt, ctx->clms_sessions_clt, ctx);
			printf("</outgoing>\n");
		}
		if (srv_s_num) {
			printf("\t\"outgoing\": ");
			printf("<incoming>\n");
			list_sessions_xml(s_srv, ctx->clms_sessions_srv, ctx);
			printf("</incoming>\n");
		}

		break;
	case FMT_TERM:
	default:
		if (clt_s_num && srv_s_num && !ctx->noheaders_set)
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Outgoing sessions"));

		if (clt_s_num)
			list_sessions_term(s_clt, ctx->clms_sessions_clt, ctx);

		if (clt_s_num && srv_s_num && !ctx->noheaders_set)
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Incoming sessions"));

		if (srv_s_num)
			list_sessions_term(s_srv, ctx->clms_sessions_srv, ctx);
		break;
	}

	return 0;
}

static int list_paths(struct ibnbd_path **p_clt, int clt_p_num,
		      struct ibnbd_path **p_srv, int srv_p_num,
		      struct ibnbd_ctx *ctx)
{
	if (!(ctx->ibnbdmode & IBNBD_CLIENT))
		clt_p_num = 0;
	if (!(ctx->ibnbdmode & IBNBD_SERVER))
		srv_p_num = 0;

	switch (ctx->fmt) {
	case FMT_CSV:
		if (clt_p_num && srv_p_num)
			printf("Outgoing paths:\n");

		if (clt_p_num)
			list_paths_csv(p_clt, ctx->clms_paths_clt, ctx);

		if (clt_p_num && srv_p_num)
			printf("Incoming paths:\n");

		if (srv_p_num)
			list_paths_csv(p_srv, ctx->clms_paths_srv, ctx);
		break;
	case FMT_JSON:
		printf("{\n");

		if (clt_p_num) {
			printf("\t\"outgoing paths\": ");
			list_paths_json(p_clt, ctx->clms_paths_clt, ctx);
		}

		if (clt_p_num && srv_p_num)
			printf(",");

		printf("\n");

		if (srv_p_num) {
			printf("\t\"incoming paths\": ");
			list_paths_json(p_srv, ctx->clms_paths_srv, ctx);
		}

		printf("\n}\n");

		break;
	case FMT_XML:
		if (clt_p_num) {
			printf("<outgoing paths>\n");
			list_paths_xml(p_clt, ctx->clms_paths_clt, ctx);
			printf("</outgoing paths>\n");
		}
		if (srv_p_num) {
			printf("<incoming paths>\n");
			list_paths_xml(p_srv, ctx->clms_paths_srv, ctx);
			printf("</incoming paths>\n");
		}

		break;
	case FMT_TERM:
	default:
		if (clt_p_num && srv_p_num && !ctx->noheaders_set)
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Outgoing paths"));

		if (clt_p_num)
			list_paths_term(p_clt, clt_p_num,
					ctx->clms_paths_clt, 0, ctx);

		if (clt_p_num && srv_p_num && !ctx->noheaders_set)
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Incoming paths"));

		if (srv_p_num)
			list_paths_term(p_srv, srv_p_num,
					ctx->clms_paths_srv, 0, ctx);
		break;
	}

	return 0;
}

static int cmd_list(void)
{
	int rc;

	switch (ctx.lstmode) {
	case LST_DEVICES:
	default:
		rc = list_devices(sds_clt, sds_clt_cnt - 1, sds_srv,
				  sds_srv_cnt - 1, &ctx);
		break;
	case LST_SESSIONS:
		rc = list_sessions(sess_clt, sess_clt_cnt - 1, sess_srv,
				   sess_srv_cnt - 1, &ctx);
		break;
	case LST_PATHS:
		rc = list_paths(paths_clt, paths_clt_cnt - 1, paths_srv,
				paths_srv_cnt - 1, &ctx);
		break;
	}

	return rc;
}

static bool match_device(struct ibnbd_sess_dev *d, const char *name)
{
	if (!strcmp(d->mapping_path, name) ||
	    !strcmp(d->dev->devname, name) ||
	    !strcmp(d->dev->devpath, name))
		return true;

	return false;
}

static int find_devices(const char *name, struct ibnbd_sess_dev **devs,
			struct ibnbd_sess_dev **res)
{
	int i, cnt = 0;

	for (i = 0; devs[i]; i++)
		if (match_device(devs[i], name))
			res[cnt++] = devs[i];

	res[cnt] = NULL;

	return cnt;
}

/*
 * Find all ibnbd devices by device name, path or mapping path
 */
static int find_devs_all(const char *name, struct ibnbd_sess_dev **ds_imp,
			    int *ds_imp_cnt, struct ibnbd_sess_dev **ds_exp,
			    int *ds_exp_cnt)
{
	int cnt_imp = 0, cnt_exp = 0;

	if (ctx.ibnbdmode & IBNBD_CLIENT)
		cnt_imp = find_devices(name, sds_clt, ds_imp);
	if (ctx.ibnbdmode & IBNBD_SERVER)
		cnt_exp = find_devices(name, sds_srv, ds_exp);

	*ds_imp_cnt = cnt_imp;
	*ds_exp_cnt = cnt_exp;

	return cnt_imp + cnt_exp;
}

static int show_device(struct ibnbd_sess_dev **clt, struct ibnbd_sess_dev **srv,
		       struct ibnbd_ctx *ctx)
{
	struct table_fld flds[CLM_MAX_CNT];
	struct ibnbd_sess_dev **ds;
	struct table_column **cs;

	if (clt[0]) {
		ds = clt;
		cs = ctx->clms_devices_clt;
	} else {
		ds = srv;
		cs = ctx->clms_devices_srv;
	}

	switch (ctx->fmt) {
	case FMT_CSV:
		list_devices_csv(ds, cs, ctx);
		break;
	case FMT_JSON:
		list_devices_json(ds, cs, ctx);
		printf("\n");
		break;
	case FMT_XML:
		list_devices_xml(ds, cs, ctx);
		break;
	case FMT_TERM:
	default:
		table_row_stringify(ds[0], flds, cs, ctx, true, 0);
		table_entry_print_term("", flds, cs, table_get_max_h_width(cs),
				       ctx->trm);
		break;
	}

	return 0;
}

static bool match_sess(struct ibnbd_sess *s, const char *name)
{
	char *at;

	if (!strcmp(name, s->sessname))
		return true;

	at = strchr(s->sessname, '@');
	if (at && (!strcmp(name, at + 1) ||
		   !strncmp(name, s->sessname, at - s->sessname)))
		return true;
	return false;
}

static struct ibnbd_sess *find_session(const char *name,
				       struct ibnbd_sess **ss)
{
	int i;

	for (i = 0; ss[i]; i++)
		if (!strcmp(name, ss[i]->sessname))
			return ss[i];

	return NULL;
}

static int find_sessions_match(const char *name, struct ibnbd_sess **ss,
			       struct ibnbd_sess **res)
{
	int i, cnt = 0;

	for (i = 0; ss[i]; i++)
		if (match_sess(ss[i], name))
			res[cnt++] = ss[i];

	res[cnt] = NULL;

	return cnt;
}

static int find_sess_all(const char *name, struct ibnbd_sess **ss_clt,
			     int *ss_clt_cnt, struct ibnbd_sess **ss_srv,
			     int *ss_srv_cnt)
{
	int cnt_srv = 0, cnt_clt = 0;

	if (ctx.ibnbdmode & IBNBD_CLIENT)
		cnt_clt = find_sessions_match(name, sess_clt, ss_clt);
	if (ctx.ibnbdmode & IBNBD_SERVER)
		cnt_srv = find_sessions_match(name, sess_srv, ss_srv);

	*ss_clt_cnt = cnt_clt;
	*ss_srv_cnt = cnt_srv;

	return cnt_clt + cnt_srv;
}

static bool match_path(struct ibnbd_path *p, const char *name)
{
	int port;
	char *at;

	if (!strcmp(p->pathname, name) ||
	    !strcmp(name, p->src_addr) ||
	    !strcmp(name, p->dst_addr) ||
	    (sscanf(name, "%d\n", &port) == 1 &&
	     p->hca_port == port) ||
	    !strcmp(name, p->hca_name))
		return true;

	at = strrchr(name, ':');
	if (!at)
		return false;

	if (strncmp(p->sess->sessname, name,
		    strlen(p->sess->sessname)))
		return false;

	if ((sscanf(at + 1, "%d\n", &port) == 1 &&
	     p->hca_port == port) ||
	    !strcmp(at + 1, p->dst_addr) ||
	    !strcmp(at + 1, p->src_addr) ||
	    !strcmp(at + 1, p->hca_name))
		return true;

	return false;
}

static int find_paths(const char *name, struct ibnbd_path **pp,
		      struct ibnbd_path **res)
{
	int i, cnt = 0;

	for (i = 0; pp[i]; i++)
		if (match_path(pp[i], name))
			res[cnt++] = pp[i];

	res[cnt] = NULL;

	return cnt;
}

static int find_paths_all(const char *name, struct ibnbd_path **pp_clt,
			  int *pp_clt_cnt, struct ibnbd_path **pp_srv,
			  int *pp_srv_cnt)
{
	int cnt_clt = 0, cnt_srv = 0;

	if (ctx.ibnbdmode & IBNBD_CLIENT)
		cnt_clt = find_paths(name, paths_clt, pp_clt);
	if (ctx.ibnbdmode & IBNBD_SERVER)
		cnt_srv = find_paths(name, paths_srv, pp_srv);

	*pp_clt_cnt = cnt_clt;
	*pp_srv_cnt = cnt_srv;

	return cnt_clt + cnt_srv;
}

static int show_path(struct ibnbd_path **pp_clt, struct ibnbd_path **pp_srv,
		     struct ibnbd_ctx *ctx)
{
	struct table_fld flds[CLM_MAX_CNT];
	struct table_column **cs;
	struct ibnbd_path **pp;

	if (pp_clt[0]) {
		pp = pp_clt;
		cs = ctx->clms_paths_clt;
	} else {
		pp = pp_srv;
		cs = ctx->clms_paths_srv;
	}

	switch (ctx->fmt) {
	case FMT_CSV:
		list_paths_csv(pp, cs, ctx);
		break;
	case FMT_JSON:
		list_paths_json(pp, cs, ctx);
		printf("\n");
		break;
	case FMT_XML:
		list_paths_xml(pp, cs, ctx);
		break;
	case FMT_TERM:
	default:
		table_row_stringify(pp[0], flds, cs, ctx, true, 0);
		table_entry_print_term("", flds, cs,
				       table_get_max_h_width(cs), ctx->trm);
		break;
	}

	return 0;
}

static int show_session(struct ibnbd_sess **ss_clt, struct ibnbd_sess **ss_srv,
			struct ibnbd_ctx *ctx)
{
	struct table_fld flds[CLM_MAX_CNT];
	struct table_column **cs, **ps;
	struct ibnbd_sess **ss;

	if (ss_clt[0]) {
		ss = ss_clt;
		cs = ctx->clms_sessions_clt;
		ps = clms_paths_sess_clt;
	} else {
		ss = ss_srv;
		cs = ctx->clms_sessions_srv;
		ps = clms_paths_sess_srv;
	}

	switch (ctx->fmt) {
	case FMT_CSV:
		list_sessions_csv(ss, cs, ctx);
		break;
	case FMT_JSON:
		list_sessions_json(ss, cs, ctx);
		printf("\n");
		break;
	case FMT_XML:
		list_sessions_xml(ss, cs, ctx);
		break;
	case FMT_TERM:
	default:
		table_row_stringify(ss[0], flds, cs, ctx, true, 0);
		table_entry_print_term("", flds, cs,
				       table_get_max_h_width(cs), ctx->trm);
		printf("%s%s%s", CLR(ctx->trm, CBLD, ss[0]->sessname));
		if (ss[0]->side == IBNBD_CLT)
			printf(" %s(%s)%s",
			       CLR(ctx->trm, CBLD, ss[0]->mp_short));
		printf("\n");
		list_paths_term(ss[0]->paths, ss[0]->path_cnt, ps, 1, ctx);

		break;
	}

	return 0;
}

static int cmd_show(void)
{
	struct ibnbd_sess_dev **ds_clt, **ds_srv;
	struct ibnbd_path **pp_clt, **pp_srv;
	struct ibnbd_sess **ss_clt, **ss_srv;
	int c_ds_clt, c_ds_srv, c_ds = 0,
	    c_pp_clt, c_pp_srv, c_pp = 0,
	    c_ss_clt, c_ss_srv, c_ss = 0, ret;

	pp_clt = calloc(paths_clt_cnt, sizeof(*pp_clt));
	pp_srv = calloc(paths_srv_cnt, sizeof(*pp_srv));
	ss_clt = calloc(sess_clt_cnt, sizeof(*ss_clt));
	ss_srv = calloc(sess_srv_cnt, sizeof(*ss_srv));
	ds_clt = calloc(sds_clt_cnt, sizeof(*ds_clt));
	ds_srv = calloc(sds_srv_cnt, sizeof(*ds_srv));

	if ((paths_clt_cnt && !pp_clt) ||
	    (paths_srv_cnt && !pp_srv) ||
	    (sess_clt_cnt && !ss_clt) ||
	    (sess_srv_cnt && !ss_srv) ||
	    (sds_clt_cnt && !ds_clt) ||
	    (sds_srv_cnt && !ds_srv)) {
		ERR(ctx.trm, "Failed to alloc memory\n");
		ret = -ENOMEM;
		goto out;
	}
	if (!ctx.lstmode_set || ctx.lstmode == LST_PATHS)
		c_pp = find_paths_all(ctx.name, pp_clt, &c_pp_clt, pp_srv,
				      &c_pp_srv);
	if (!ctx.lstmode_set || ctx.lstmode == LST_SESSIONS)
		c_ss = find_sess_all(ctx.name, ss_clt, &c_ss_clt, ss_srv,
				     &c_ss_srv);
	if (!ctx.lstmode_set || ctx.lstmode == LST_DEVICES)
		c_ds = find_devs_all(ctx.name, ds_clt, &c_ds_clt, ds_srv,
				     &c_ds_srv);
	if (c_pp + c_ss + c_ds > 1) {
		ERR(ctx.trm, "Multiple entries match '%s'\n", ctx.name);
		if (c_pp) {
			printf("Paths:\n");
			list_paths(pp_clt, c_pp_clt, pp_srv, c_pp_srv, &ctx);
		}
		if (c_ss) {
			printf("Sessions:\n");
			list_sessions(ss_clt, c_ss_clt, ss_srv, c_ss_srv, &ctx);
		}
		if (c_ds) {
			printf("Devices:\n");
			list_devices(ds_clt, c_ds_clt, ds_srv, c_ds_srv, &ctx);
		}
		ret = -EINVAL;
		goto out;
	}

	if (c_ds)
		ret = show_device(ds_clt, ds_srv, &ctx);
	else if (c_ss)
		ret = show_session(ss_clt, ss_srv, &ctx);
	else if (c_pp)
		ret = show_path(pp_clt, pp_srv, &ctx);
	else {
		ERR(ctx.trm, "There is no entry matching '%s'\n", ctx.name);
		ret = -ENOENT;
	}
out:
	free(ds_clt);
	free(ds_srv);
	free(pp_clt);
	free(pp_srv);
	free(ss_clt);
	free(ss_srv);
	return ret;
}

static int show_devices(const char *name, struct ibnbd_ctx *ctx)
{
	struct ibnbd_sess_dev **ds_clt, **ds_srv;
	int c_ds_clt, c_ds_srv, c_ds = 0, ret;

	ds_clt = calloc(sds_clt_cnt, sizeof(*ds_clt));
	ds_srv = calloc(sds_srv_cnt, sizeof(*ds_srv));

	if ((sds_clt_cnt && !ds_clt) ||
	    (sds_srv_cnt && !ds_srv)) {
		ERR(ctx->trm, "Failed to alloc memory\n");
		ret = -ENOMEM;
		goto out;
	}
	c_ds = find_devs_all(name, ds_clt, &c_ds_clt, ds_srv, &c_ds_srv);
	if (c_ds > 1) {
		ERR(ctx->trm, "Multiple devices match '%s'\n", name);

		printf("Devices:\n");
		list_devices(ds_clt, c_ds_clt, ds_srv, c_ds_srv, ctx);

		ret = -EINVAL;
		goto out;
	}

	if (c_ds) {
		ret = show_device(ds_clt, ds_srv, ctx);
	} else {
		ERR(ctx->trm, "There is no device matching '%s'\n", name);
		ret = -ENOENT;
	}
out:
	free(ds_clt);
	free(ds_srv);
	return ret;
}

static int show_sessions(const char *name, struct ibnbd_ctx *ctx)
{
	struct ibnbd_sess **ss_clt, **ss_srv;
	int c_ss_clt, c_ss_srv, c_ss = 0, ret;

	ss_clt = calloc(sess_clt_cnt, sizeof(*ss_clt));
	ss_srv = calloc(sess_srv_cnt, sizeof(*ss_srv));

	if ((sess_clt_cnt && !ss_clt) ||
	    (sess_srv_cnt && !ss_srv)) {
		ERR(ctx->trm, "Failed to alloc memory\n");
		ret = -ENOMEM;
		goto out;
	}
	c_ss = find_sess_all(name, ss_clt, &c_ss_clt, ss_srv, &c_ss_srv);

	if (c_ss > 1) {
		ERR(ctx->trm, "Multiple sessions match '%s'\n", name);

		printf("Sessions:\n");
		list_sessions(ss_clt, c_ss_clt, ss_srv, c_ss_srv, ctx);

		ret = -EINVAL;
		goto out;
	}

	if (c_ss) {
		ret = show_session(ss_clt, ss_srv, ctx);
	} else {
		ERR(ctx->trm, "There is no session matching '%s'\n", name);
		ret = -ENOENT;
	}
out:
	free(ss_clt);
	free(ss_srv);
	return ret;
}

static int show_paths(const char *name, struct ibnbd_ctx *ctx)
{
	struct ibnbd_path **pp_clt, **pp_srv;
	int c_pp_clt, c_pp_srv, c_pp = 0, ret;

	pp_clt = calloc(paths_clt_cnt, sizeof(*pp_clt));
	pp_srv = calloc(paths_srv_cnt, sizeof(*pp_srv));

	if ((paths_clt_cnt && !pp_clt) ||
	    (paths_srv_cnt && !pp_srv)) {
		ERR(ctx->trm, "Failed to alloc memory\n");
		ret = -ENOMEM;
		goto out;
	}
	c_pp = find_paths_all(name, pp_clt, &c_pp_clt, pp_srv, &c_pp_srv);

	if (c_pp > 1) {
		ERR(ctx->trm, "Multiple paths match '%s'\n", name);

		printf("Paths:\n");
		list_paths(pp_clt, c_pp_clt, pp_srv, c_pp_srv, ctx);

		ret = -EINVAL;
		goto out;
	}

	if (c_pp) {
		ret = show_path(pp_clt, pp_srv, ctx);
	} else {
		ERR(ctx->trm, "There is no path matching '%s'\n", name);
		ret = -ENOENT;
	}
out:
	free(pp_clt);
	free(pp_srv);
	return ret;
}

static int parse_name(int argc, const char *argv[], int i,
		      struct ibnbd_ctx *ctx)
{
	int j = i + 1;

	if (j >= argc) {
		ERR(ctx->trm, "Please specify the <name> argument\n");
		return i;
	}

	ctx->name = argv[j];

	return j + 1;
}

static int parse_name_help(int argc, const char *argv[], const char *what,
			   const struct cmd *cmd, struct ibnbd_ctx *ctx)
{
	if (argc <= 0) {
		ERR(ctx->trm, "Please specify the %s argument\n", what);
		return -EINVAL;
	}
	if (!strcmp(*argv, "help")) {
		parse_help(argc, argv, 0, NULL, ctx);

		cmd->help(cmd, ctx);
		return -EAGAIN;
	}
	ctx->name = *argv;

	return 0;
}

static void help_show(const struct cmd *cmd,
		      const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<name> [path] ", ctx);

	printf("\nArguments:\n");
	print_opt("<name>",
		  "Name of a local or a remote block device, session name, path name or remote hostname.");
	print_opt("",
		  "I.e. ibnbd0, /dev/ibnbd0, d12aef94-4110-4321-9373-3be8494a557b, ps401a-1@st401b-2, st401b-2, <ip1>@<ip2>, etc.");
	print_opt("",
		  "In order to display path information, path name or identifier");
	print_opt("", "has to be provided, i.e. st401b-2:1.");

	printf("\nOptions:\n");
	help_fields();

	printf("%s%s%s%s\n", HPRE, CLR(ctx->trm, CDIM, "Device Fields"));
	print_fields(def_clms_devices_clt, def_clms_devices_srv,
		     all_clms_devices, IBNBD_BOTH);

	printf("%s%s%s%s\n", HPRE, CLR(ctx->trm, CDIM, "Sessions Fields"));
	print_fields(def_clms_sessions_clt, def_clms_sessions_srv,
		     all_clms_sessions, IBNBD_BOTH);

	printf("%s%s%s%s\n", HPRE, CLR(ctx->trm, CDIM, "Paths Fields"));
	print_fields(def_clms_paths_clt, def_clms_paths_srv,
		     all_clms_paths, IBNBD_BOTH);

	printf("%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_opt("{mode}",
		  "Information to print: device|session|path. Default: device.");

	print_sarg_descr("help");
}

static void help_show_devices(const struct cmd *cmd,
			      const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "devices", ctx);

	if (!help_print_fields(ctx)) {

		printf("\nArguments:\n");
		print_opt("<name>",
			  "Name of a local or a remote block device.");
		print_opt("",
			  "I.e. ibnbd0, /dev/ibnbd0, d12aef94-4110-4321-9373-3be8494a557b.");

		printf("\nOptions:\n");
	}
	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(def_clms_devices_clt, def_clms_devices_srv,
			     all_clms_devices, ctx->ibnbdmode);
		if (help_print_fields(ctx))
			return;
	}

	printf("%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");

	print_opt("help", "Display help and exit. [fields|all]");
}

static void help_show_sessions(const struct cmd *cmd,
			       const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "sessions", ctx);

	if (!help_print_fields(ctx)) {

		printf("\nArguments:\n");
		print_opt("<name>",
			  "Session name or remote hostname.");
		print_opt("",
			  "I.e. ps401a-1@st401b-2, st401b-2, <ip1>@<ip2>, etc.");

		printf("\nOptions:\n");
	}
	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(def_clms_sessions_clt, def_clms_sessions_srv,
			     all_clms_sessions, ctx->ibnbdmode);
		if (help_print_fields(ctx))
			return;
	}

	printf("%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");

	print_opt("help", "Display help and exit. [fields|all]");
}

static void help_show_paths(const struct cmd *cmd,
			    const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "paths", ctx);

	if (!help_print_fields(ctx)) {

		printf("\nArguments:\n");
		print_opt("<name>",
			  "In order to display path information, path name or identifier");
		print_opt("", "has to be provided, i.e. st401b-2:1.");

		printf("\nOptions:\n");
	}

	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(def_clms_paths_clt, def_clms_paths_srv,
			     all_clms_paths, ctx->ibnbdmode);

		if (help_print_fields(ctx))
			return;
	}

	printf("%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");

	print_opt("help", "Display help and exit. [fields|all]");
}

static void help_map(const struct cmd *cmd,
		     const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<path> from <server> ", ctx);

	printf("\nArguments:\n");
	print_opt("<device>", "Path to the device to be mapped on server side");
	print_opt("from <server>",
		  "Address, hostname or session name of the server");

	printf("\nOptions:\n");
	print_opt("<path>", "Path(s) to establish: [src_addr@]dst_addr");
	print_opt("", "Address is [ip:]<ipv4>, [ip:]<ipv6> or gid:<gid>");

	print_opt("{io_mode}",
		  "IO Mode to use on server side: fileio|blockio. Default: blockio");
	print_opt("{rw}",
		  "Access permission on server side: ro|rw|migration. Default: rw");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static bool is_ip4(const char *arg)
{
	/* TODO */
	return false;
}

static bool is_ip6(const char *arg)
{
	/* TODO */
	return false;
}

static bool is_gid(const char *arg)
{
	/* TODO */
	return is_ip6(arg);
}

static int parse_path(const char *arg)
{
	const char *src, *dst;
	char *d;

	d = strchr(arg, '@');
	if (d) {
		src = arg;
		dst = d + 1;
	} else {
		src = NULL;
		dst = arg;
	}

	if (src && !is_ip4(src) && !is_ip6(src) && !is_gid(src))
		return -EINVAL;

	if (!is_ip4(dst) && !is_ip6(dst) && !is_gid(dst))
		return -EINVAL;

	ctx.paths[ctx.path_cnt].src = src;
	ctx.paths[ctx.path_cnt].dst = dst;

	ctx.path_cnt++;

	return 0;
}

static void ibnbd_TODO(const struct cmd *cmd, const struct ibnbd_ctx *ctx)
{
	if (ctx->trm)
		printf("%s%s", colors[CMAG], colors[CBLD]);
	printf("Command %s is not implemented yet.\n", cmd->cmd);
	if (ctx->trm)
		printf("%s", colors[CNRM]);
}

static int client_devices_map(const char *from_session, const char *device_name,
			      const struct ibnbd_ctx *ctx)
{
	char cmd[4096], sessname[NAME_MAX];
	struct ibnbd_sess *sess;
	int i, cnt = 0, ret;

	if (!parse_path(ctx->from)) {
		/* user provided only paths to establish
		 * -> generate sessname
		 */
		strcpy(sessname, "clt@srv"); /* TODO */
	} else
		strcpy(sessname, from_session);

	sess = find_session(sessname, sess_clt);

	if (!sess && !ctx->path_cnt) {
		ERR(ctx->trm,
		    "Client session '%s' not found. Please provide at least one path to establish a new one.\n",
		    from_session);
		return -EINVAL;
	}

	if (sess && ctx->path_cnt)
		INF(ctx->verbose_set,
		    "Session '%s' exists. Provided paths will be ignored by the driver. Please use addpath to add a path to an existsing sesion.\n",
		    from_session);

	cnt = snprintf(cmd, sizeof(cmd), "sessname=%s", sessname);
	cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt, " device_path=%s",
			device_name);

	for (i = 0; i < ctx->path_cnt; i++)
		cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt, " path=%s@%s",
				ctx->paths[i].src, ctx->paths[i].dst);

	if (sess)
		for (i = 0; i < sess->path_cnt; i++)
			cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt,
					" path=%s@%s", sess->paths[i]->src_addr,
					sess->paths[i]->dst_addr);

	if (ctx->io_mode_set)
		cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt, " io_mode=%s",
				ctx->io_mode);

	if (ctx->access_mode_set)
		cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt, " access_mode=%s",
				ctx->access_mode);

	errno = 0;
	ret = printf_sysfs(PATH_IBNBD_CLT, "map_device", "%s", cmd);
	ret = (ret < 0 ? ret : errno);
	if (ret)
		ERR(ctx->trm, "Failed to map device: %m (%d)\n", ret);

	return ret;
}

static int cmd_map(void)
{
	return client_devices_map(ctx.from, ctx.name, &ctx);
}

static struct ibnbd_sess_dev *find_single_device(const char *name,
						 struct ibnbd_sess_dev **devs)
{
	struct ibnbd_sess_dev *ds = NULL, **res;
	int cnt;

	if (!sds_clt_cnt) {
		ERR(ctx.trm,
		    "Device '%s' not found: no devices mapped\n", name);
		return NULL;
	}

	res = calloc(sds_clt_cnt, sizeof(*res));
	if (!res) {
		ERR(ctx.trm, "Failed to allocate memory\n");
		return NULL;
	}

	cnt = find_devices(name, devs, res);
	if (!cnt) {
		ERR(ctx.trm, "Device '%s' not found\n", name);
		goto free;
	}

	if (cnt > 1) {
		ERR(ctx.trm,
		"Please specify an exact path. There are multiple devices matching '%s':\n",
		name);
		list_devices(devs, cnt, &ds, 0, &ctx);
		goto free;
	}

	ds = res[0];

free:
	free(res);
	return ds;
}

static int client_devices_resize(const char *device_name, uint64_t size_sect,
				 const struct ibnbd_ctx *ctx)
{
	struct ibnbd_sess_dev *ds;
	char tmp[PATH_MAX];
	int ret;

	ds = find_single_device(device_name, sds_clt);
	if (!ds) {
		ERR(ctx->trm, "Device %s does not exist\n", device_name);
		return -EINVAL;
	}
	sprintf(tmp, "/sys/block/%s/ibnbd/", ds->dev->devname);
	errno = 0;
	ret = printf_sysfs(tmp, "resize", "%s", size_sect);
	ret = (ret < 0 ? ret : errno);
	if (ret)
		ERR(ctx->trm, "Failed to resize %s: %m (%d)\n",
		    ds->dev->devname, ret);

	return ret;
}

static int cmd_resize(void)
{
	if (!ctx.size_set) {
		ERR(ctx.trm,
		    "Please provide the size of the device to be configured\n");
		return -EINVAL;
	}
	return client_devices_resize(ctx.name, ctx.size_sect, &ctx);
}

static void help_resize(const struct cmd *cmd,
			const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<device name or path or mapping path> ", ctx);

	printf("\nArguments:\n");
	print_opt("<device>", "Name of the device to be resized");
	print_opt("<size>", "New size of the device in bytes");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static void help_unmap(const struct cmd *cmd,
		       const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<device name or path or mapping path> ", ctx);

	printf("\nArguments:\n");
	print_opt("<device>", "Name of the device to be unmapped");

	printf("\nOptions:\n");
	print_sarg_descr("force");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int client_devices_unmap(const char *device_name, bool force,
				const struct ibnbd_ctx *ctx)
{
	struct ibnbd_sess_dev *ds;
	char tmp[PATH_MAX];
	int ret;

	ds = find_single_device(device_name, sds_clt);
	if (!ds)
		return -EINVAL;

	sprintf(tmp, "/sys/block/%s/ibnbd/", ds->dev->devname);
	errno = 0;
	ret = printf_sysfs(tmp, "unmap_device", "%s",
			   force ? "force" : "normal");
	ret = (ret < 0 ? ret : errno);
	if (ret)
		ERR(ctx->trm, "Failed to %sunmap '%s': %m (%d)\n",
		    force ? "force-" : "",
		    ds->dev->devname, ret);

	return ret;
}

static int cmd_unmap(void)
{
	return client_devices_unmap(ctx.name, ctx.force_set, &ctx);
}

static void help_remap(const struct cmd *cmd,
		       const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<device name> ", ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>", "Identifier of a device to be remapped.");

	printf("\nOptions:\n");
	print_sarg_descr("force");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static void help_remap_session(const struct cmd *cmd,
			       const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<session name> ", ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>",
		  "Identifier of a session to remap all devices on.");

	printf("\nOptions:\n");
	print_sarg_descr("force");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int cmd_remap(void)
{
	printf("TODO\n");
	return 0;
}

static void help_reconnect(const struct cmd *cmd,
			   const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<path or session> ", ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>",
		  "Name or identifier of a session or of a path:");
	print_opt("", "[sessname], [pathname], [sessname:port], etc.");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static void help_reconnect_session(const struct cmd *cmd,
				   const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<session> ", ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>",
		  "Name or identifier of a session:");
	print_opt("", "[sessname]");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static void help_reconnect_path(const struct cmd *cmd,
				const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<path> ", ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>",
		  "Name or identifier of a path:");
	print_opt("", "[pathname], [sessname:port], etc.");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int cmd_reconnect(void)
{
	printf("TODO\n");
	return 0;
}

static void help_disconnect(const struct cmd *cmd,
			    const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<path or session> ", ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>",
		  "Name or identifier of a session or of a path:");
	print_opt("", "[sessname], [pathname], [sessname:port], etc.");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static void help_disconnect_session(const struct cmd *cmd,
				    const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<session> ", ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>", "Name or identifier of a session:");
	print_opt("", "[sessname]");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static void help_disconnect_path(const struct cmd *cmd,
				 const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<path> ", ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>", "Name or identifier of of a path:");
	print_opt("", "[pathname], [sessname:port], etc.");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int cmd_disconnect(void)
{
	printf("TODO\n");
	return 0;
}

static void help_addpath(const struct cmd *cmd,
			 const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<session> <path> ", ctx);

	printf("\nArguments:\n");
	print_opt("<session>",
		  "Name of the session to add the new path to");
	print_opt("<path>",
		  "Path to be added: [src_addr,]dst_addr");
	print_opt("",
		  "Address is of the form ip:<ipv4>, ip:<ipv6> or gid:<gid>");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int cmd_addpath(void)
{
	printf("TODO\n");
	return 0;
}

static void help_help(const struct cmd *cmd,
		      const struct ibnbd_ctx *ctx);

static void help_delpath(const struct cmd *cmd,
			 const struct ibnbd_ctx *ctx)
{
	cmd_print_usage(cmd, "<path> ", ctx);

	printf("\nArguments:\n");
	print_opt("<path>",
		  "Name or any unique identifier of a path:");
	print_opt("", "[pathname], [sessname:port], etc.");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int cmd_delpath(void)
{
	printf("TODO\n");
	return 0;
}

static struct cmd _cmd_list =
	{TOK_LIST, "list",
		"List information on all",
		"s",
		"List block device or transport related information: devices, sessions, paths, etc.",
		cmd_list, NULL, help_list};
static struct cmd _cmd_list_devices =
	{TOK_LIST, "list",
		"List information on all",
		"s",
		"List information on devices.",
		cmd_list, NULL, help_list_devices};
static struct cmd _cmd_list_sessions =
	{TOK_LIST, "list",
		"List information on all",
		"s",
		"List information on sessions.",
		cmd_list, NULL, help_list_sessions};
static struct cmd _cmd_list_paths =
	{TOK_LIST, "list",
		"List information on all",
		"s",
		"List information on paths.",
		cmd_list, NULL, help_list_paths};
static struct cmd _cmd_show =
	{TOK_SHOW, "show",
		"Show information about a",
		"",
		"Show information about an ibnbd block- or transport- item: device, session or path.",
		cmd_show, parse_name, help_show};
static struct cmd _cmd_show_devices =
	{TOK_SHOW, "show",
		"Show information about a",
		"",
		"Show information about an ibnbd block device.",
		cmd_show, parse_name, help_show_devices};
static struct cmd _cmd_show_sessions =
	{TOK_SHOW, "show",
		"Show information about a",
		"",
		"Show information about an ibnbd session.",
		cmd_show, parse_name, help_show_sessions};
static struct cmd _cmd_show_paths =
	{TOK_SHOW, "show",
		"Show information about a",
		"",
		"Show information about an ibnbd transport path.",
		cmd_show, parse_name, help_show_paths};
static struct cmd _cmd_map =
	{TOK_MAP, "map",
		"Map a",
		" from a given server",
		"Map a device from a given server",
		 cmd_map, parse_name, help_map};
static struct cmd _cmd_resize =
	{TOK_RESIZE, "resize",
		"Resize a mapped",
		"",
		"Change size of a mapped device",
		 cmd_resize, parse_name, help_resize};
static struct cmd _cmd_unmap =
	{TOK_UNMAP, "unmap",
		"Unmap an imported",
		"",
		"Umap a given imported device",
		cmd_unmap, parse_name, help_unmap};
static struct cmd _cmd_remap =
	{TOK_REMAP, "remap",
		"Remap a",
		"",
		"Unmap and map again an imported device",
		 cmd_remap, parse_name, help_remap};
static struct cmd _cmd_remap_session =
	{TOK_REMAP, "remap",
		"Remap all devicess on a",
		"",
		"Unmap and map again all devices of a given session",
		 cmd_remap, parse_name, help_remap_session};
static struct cmd _cmd_disconnect =
	{TOK_DISCONNECT, "disconnect",
		"Disconnect a",
		"",
		"Disconnect a path or all paths on a given session",
		cmd_disconnect, parse_name, help_disconnect};
static struct cmd _cmd_disconnect_session =
	{TOK_DISCONNECT, "disconnect",
		"Disconnect a",
		"",
		"Disconnect all paths on a given session",
		cmd_disconnect, parse_name, help_disconnect_session};
static struct cmd _cmd_disconnect_path =
	{TOK_DISCONNECT, "disconnect",
		"Disconnect a",
		"",
		"Disconnect a path a given session",
		cmd_disconnect, parse_name, help_disconnect_path};
static struct cmd _cmd_reconnect =
	{TOK_RECONNECT, "reconnect",
		"Reconnect a",
		"",
		"Disconnect and connect again a path or a whole session",
		 cmd_reconnect, parse_name, help_reconnect};
static struct cmd _cmd_reconnect_session =
	{TOK_RECONNECT, "reconnect",
		"Reconnect a",
		"",
		"Disconnect and connect again a whole session",
		 cmd_reconnect, parse_name, help_reconnect_session};
static struct cmd _cmd_reconnect_path =
	{TOK_RECONNECT, "reconnect",
		"Reconnect a",
		"",
		"Disconnect and connect again a single path of a session",
		 cmd_reconnect, parse_name, help_reconnect_path};
static struct cmd _cmd_addpath =
	{TOK_ADD, "addpath",
		"Add a path to an existing session",
		"",
		"Add a new path to an existing session",
		 cmd_addpath, parse_name, help_addpath};
static struct cmd _cmd_add =
	{TOK_ADD, "add",
		"Add a",
		" to an existing session",
		"Add a new path to an existing session",
		 cmd_addpath, parse_name, help_addpath};
static struct cmd _cmd_delpath =
	{TOK_DELETE, "delpath",
		"Delete a path",
		"",
		"Delete a given path from the corresponding session",
		 cmd_delpath, parse_name, help_delpath};
static struct cmd _cmd_delete =
	{TOK_DELETE, "delete",
		"Delete a",
		"",
		"Delete a given path from the corresponding session",
		 cmd_delpath, parse_name, help_delpath};
static struct cmd _cmd_help =
	{TOK_HELP, "help",
		"Display help on",
		"s",
		"Display help message and exit.",
		cmd_help, NULL, help_help};
static struct cmd _cmd_null =
		{ 0 };

static struct cmd *cmds[] = {
	&_cmd_list,
	&_cmd_show,
	&_cmd_map,
	&_cmd_resize,
	&_cmd_unmap,
	&_cmd_remap,
	&_cmd_disconnect,
	&_cmd_reconnect,
	&_cmd_addpath,
	&_cmd_add,
	&_cmd_delpath,
	&_cmd_delete,
	&_cmd_help,
	&_cmd_null
};

static struct cmd *cmds_client_sessions[] = {
	&_cmd_list_sessions,
	&_cmd_show_sessions,
	&_cmd_reconnect_session,
	&_cmd_remap_session,
	&_cmd_help,
	&_cmd_null
};

static struct cmd *cmds_client_devices[] = {
	&_cmd_list_devices,
	&_cmd_show_devices,
	&_cmd_map,
	&_cmd_resize,
	&_cmd_unmap,
	&_cmd_remap,
	&_cmd_help,
	&_cmd_null
};

static struct cmd *cmds_client_paths[] = {
	&_cmd_list_paths,
	&_cmd_show_paths,
	&_cmd_disconnect_path,
	&_cmd_reconnect_path,
	&_cmd_add,
	&_cmd_delete,
	&_cmd_help,
	&_cmd_null
};

static struct cmd *cmds_server_sessions[] = {
	&_cmd_list_sessions,
	&_cmd_show_sessions,
	&_cmd_disconnect_session,
	&_cmd_help,
	&_cmd_null
};

static struct cmd *cmds_server_devices[] = {
	&_cmd_list_devices,
	&_cmd_show_devices,
	&_cmd_help,
	&_cmd_null
};

static struct cmd *cmds_server_paths[] = {
	&_cmd_list_paths,
	&_cmd_show_paths,
	&_cmd_disconnect_path,
	&_cmd_help,
	&_cmd_null
};

static void help_help(const struct cmd *cmd,
		      const struct ibnbd_ctx *ctx)
{
	print_help(ctx->pname, cmds, ctx);
}

static int cmd_help(void)
{
	print_help(ctx.pname, cmds, &ctx);
	return 0;
}

static int levenstein_compare(int d1, int d2, const char *s1, const char *s2)
{
	return d1 != d2 ? d1 - d2 : strcmp(s1, s2);
}

static int cmd_compare(const void *p1, const void *p2)
{
	const struct cmd *const*c1 = p1;
	const struct cmd *const*c2 = p2;

	return levenstein_compare((*c1)->dist, (*c2)->dist,
				  (*c1)->cmd, (*c2)->cmd);
}

static int sarg_compare(const void *p1, const void *p2)
{
	const struct sarg *const*c1 = p1;
	const struct sarg *const*c2 = p2;

	return levenstein_compare((*c1)->dist, (*c2)->dist,
				  (*c1)->str, (*c2)->str);
}

static void handle_unknown_cmd(const char *cmd, struct cmd *cmds[])
{
	struct cmd **cs;
	size_t len = 0, cnt = 0;

	printf("Unknown: %s\n", cmd);

	for (cs = cmds; (*cs)->cmd; cs++) {
		(*cs)->dist = levenshtein((*cs)->cmd, cmd, 1, 2, 1, 0) + 1;
		if (strlen((*cs)->cmd) < 2)
			(*cs)->dist += 3;
		len++;
		if ((*cs)->dist < 4)
			cnt++;
	}

	if (!cnt || cnt == len) {

		if (len > 7)
			return;

		cnt = len;
	} else {

		qsort(cmds, len, sizeof(*cmds), cmd_compare);
		printf("Did you mean:\n");
	}

	for (len = 0; len < cnt; len++)
		printf("\t%s\n", cmds[len]->cmd);
}

static void handle_unknown_sarg(const char *sarg, struct sarg *sargs[])
{
	struct sarg **cs;
	size_t len = 0, cnt = 0, i;

	printf("Unknown: %s\n", sarg);

	for (cs = sargs; (*cs)->str; cs++) {
		(*cs)->dist = levenshtein((*cs)->str, sarg, 1, 2, 1, 0) + 1;
		if (strlen((*cs)->str) < 2)
			(*cs)->dist += 3;
		len++;
		if ((*cs)->dist < 4)
			cnt++;
	}

	if (!cnt || cnt == len) {

		if (len > 7)
			return;
	} else {

		qsort(sargs, len, sizeof(*sargs), sarg_compare);
		printf("Did you mean:\n");
	}

	for (i = 0; i < cnt; i++)
		printf("\t%s\n", sargs[i]->str);
}

static int parse_precision(const char *str,
			   struct ibnbd_ctx *ctx)
{
	unsigned int prec;
	char e;

	if (strncmp(str, "prec", 4))
		return -EINVAL;

	if (str[4] == '=')
		str += 5;
	else
		str += 4;
	if (sscanf(str, "%u%c\n", &prec, &e) != 2)
		return -EINVAL;

	ctx->prec = prec;
	ctx->prec_set = true;

	return 0;
}

static const char *comma = ",";

static int parse_clt_devices_clms(const char *arg, struct ibnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_devices_clt,
				    ctx->clms_devices_clt, CLM_MAX_CNT);
}

static int parse_srv_devices_clms(const char *arg, struct ibnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_devices_srv,
				    ctx->clms_devices_srv, CLM_MAX_CNT);
}

static int parse_clt_sessions_clms(const char *arg, struct ibnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_sessions_clt,
				    ctx->clms_sessions_clt, CLM_MAX_CNT);
}

static int parse_srv_sessions_clms(const char *arg, struct ibnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_sessions_srv,
				    ctx->clms_sessions_srv, CLM_MAX_CNT);
}

static int parse_clt_paths_clms(const char *arg, struct ibnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_paths_clt,
				    ctx->clms_paths_clt, CLM_MAX_CNT);
}

static int parse_srv_paths_clms(const char *arg, struct ibnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_paths_srv,
				    ctx->clms_paths_srv, CLM_MAX_CNT);
}

static int parse_sign(char s)
{
	if (s == '+')
		ctx.sign = 1;
	else if (s == '-')
		ctx.sign = -1;
	else
		ctx.sign = 0;

	return ctx.sign;
}

static int parse_size(const char *str)
{
	uint64_t size;

	if (parse_sign(*str))
		str++;

	if (str_to_size(str, &size))
		return -EINVAL;

	ctx.size_sect = size >> 9;
	ctx.size_set = 1;

	return 0;
}

static void init_ibnbd_ctx(struct ibnbd_ctx *ctx)
{
	memcpy(&(ctx->clms_devices_clt), &def_clms_devices_clt,
	       ARRSIZE(def_clms_devices_clt) * sizeof(all_clms_devices[0]));
	memcpy(&(ctx->clms_devices_srv), &def_clms_devices_srv,
	       ARRSIZE(def_clms_devices_srv) * sizeof(all_clms_devices[0]));

	memcpy(&(ctx->clms_sessions_clt), &def_clms_sessions_clt,
	       ARRSIZE(def_clms_sessions_clt) * sizeof(all_clms_sessions[0]));
	memcpy(&(ctx->clms_sessions_srv), &def_clms_sessions_srv,
	       ARRSIZE(def_clms_sessions_srv) * sizeof(all_clms_sessions[0]));

	memcpy(&(ctx->clms_paths_clt), &def_clms_paths_clt,
	       ARRSIZE(def_clms_paths_clt) * sizeof(all_clms_paths[0]));
	memcpy(&(ctx->clms_paths_srv), &def_clms_paths_srv,
	       ARRSIZE(def_clms_paths_srv) * sizeof(all_clms_paths[0]));
}

static void ibnbd_ctx_default(struct ibnbd_ctx *ctx)
{
	if (!ctx->lstmode_set)
		ctx->lstmode = LST_DEVICES;

	if (!ctx->fmt_set)
		ctx->fmt = FMT_TERM;

	if (!ctx->prec_set)
		ctx->prec = 3;

	if (!ctx->ibnbdmode_set) {
		if (sess_clt[0])
			ctx->ibnbdmode |= IBNBD_CLIENT;
		if (sess_srv[0])
			ctx->ibnbdmode |= IBNBD_SERVER;
	}
}

int cmd_client_sessions(int argc, const char *argv[], struct ibnbd_ctx *ctx);
int cmd_client_devices(int argc, const char *argv[], struct ibnbd_ctx *ctx);
int cmd_client_paths(int argc, const char *argv[], struct ibnbd_ctx *ctx);
int cmd_client(int argc, const char *argv[], struct ibnbd_ctx *ctx);
int cmd_server_sessions(int argc, const char *argv[], struct ibnbd_ctx *ctx);
int cmd_server_devices(int argc, const char *argv[], struct ibnbd_ctx *ctx);
int cmd_server_paths(int argc, const char *argv[], struct ibnbd_ctx *ctx);
int cmd_server(int argc, const char *argv[], struct ibnbd_ctx *ctx);

int cmd_start(int argc, const char *argv[], struct ibnbd_ctx *ctx)
{
	int err = 0;
	const struct sarg *sarg;

	if (argc < 1) {
		ERR(ctx->trm, "mode not specified\n");
		usage_sarg(ctx->pname, sargs_mode, ctx);
		err = -EINVAL;
	}
	if (err >= 0) {
		sarg = find_sarg(*argv, sargs_mode);
		if (!sarg) {
			usage_sarg(ctx->pname, sargs_mode, ctx);
			handle_unknown_sarg(*argv, sargs_mode);
			err = -EINVAL;
		} else {
			(void) sarg->parse(argc, argv, 0, sarg, ctx);
		}
	}
	argc--; argv++;

	if (err >= 0) {
		switch (sarg->tok) {
		case TOK_CLIENT:
			err = cmd_client(argc, argv, ctx);
			break;
		case TOK_SERVER:
			err = cmd_server(argc, argv, ctx);
			break;
		case TOK_BOTH:
			err = cmd_server(argc, argv, ctx);
			ERR(ctx->trm,
			    "both client and server is not a legal use case\n");
			usage_sarg(ctx->pname, sargs_mode, ctx);
			err = -EINVAL;
			break;
		default:
			usage_sarg(ctx->pname, sargs_mode, ctx);
			handle_unknown_sarg(*argv, sargs_mode);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_client(int argc, const char *argv[], struct ibnbd_ctx *ctx)

{
	int err = 0;
	const struct sarg *sarg;

	if (argc < 1) {
		ERR(ctx->trm, "no object specified\n");
		usage_sarg("ibnbd client", sargs_object_type_help_client, ctx);
		err = -EINVAL;
	}
	if (err >= 0) {
		sarg = find_sarg(*argv, sargs_object_type);
		if (!sarg) {
			usage_sarg("ibnbd client",
				   sargs_object_type_help_client, ctx);
			handle_unknown_sarg(*argv, sargs_object_type);
			err = -EINVAL;
		} else {
			(void) sarg->parse(argc, argv, 0, sarg, ctx);
		}
	}

	if (err >= 0) {
		switch (sarg->tok) {
		case TOK_DEVICES:
			err = cmd_client_devices(--argc, ++argv, ctx);
			break;
		case TOK_SESSIONS:
			err = cmd_client_sessions(--argc, ++argv, ctx);
			break;
		case TOK_PATHS:
			err = cmd_client_paths(--argc, ++argv, ctx);
			break;
		case TOK_HELP:
			help_sarg("client", sargs_object_type_help_client, ctx);
			break;
		default:
			handle_unknown_sarg(*argv, sargs_object_type);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_server(int argc, const char *argv[], struct ibnbd_ctx *ctx)

{
	int err = 0;
	const struct sarg *sarg;

	if (argc < 1) {
		ERR(ctx->trm, "no object specified\n");
		usage_sarg("ibnbd server", sargs_object_type_help_server, ctx);
		err = -EINVAL;
	}
	if (err >= 0) {
		sarg = find_sarg(*argv, sargs_object_type);
		if (!sarg) {
			usage_sarg("ibnbd server",
				   sargs_object_type_help_server, ctx);
			handle_unknown_sarg(*argv, sargs_object_type);
			err = -EINVAL;
		} else {
			(void) sarg->parse(argc, argv, 0, sarg, ctx);
		}
	}

	if (err >= 0) {
		switch (sarg->tok) {
		case TOK_DEVICES:
			err = cmd_server_devices(--argc, ++argv, ctx);
			break;
		case TOK_SESSIONS:
			err = cmd_server_sessions(--argc, ++argv, ctx);
			break;
		case TOK_PATHS:
			err = cmd_server_paths(--argc, ++argv, ctx);
			break;
		case TOK_HELP:
			help_sarg("server", sargs_object_type_help_server, ctx);
			break;
		default:
			handle_unknown_sarg(*argv, sargs_object_type);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

/**
 * Parse all the possible parameters to list or show commands.
 * The results are collected in the ibnbd_ctx struct
 *
 *  Assumtions:
 * * all types  accept the same parameters (except for collums)
 * * it is always the end of a command line, so a left over argument
 *   that can not be interpreded is an error
 *
 * Returns:
 * > 0 number of arguments accepted
 * < 0 error code
 * == 0 there are no arguments to the list command
 */
int parse_list_parameters(int argc, const char *argv[], struct ibnbd_ctx *ctx,
		int (*parse_clms)(const char *arg, struct ibnbd_ctx *ctx),
		const struct cmd *cmd)
{
	int err = 0; int start_argc = argc;
	const struct sarg *sarg;

	while (argc && err >= 0) {
		/* parse the list flags */
		sarg = find_sarg(*argv, sargs_list_parameters);
		if (sarg) {
			err = sarg->parse(argc, argv, 0, sarg, ctx);
			if (err > 0) {
				argc -= err; argv += err;
				continue;
			}
		}
		/* parse collumn parameters */
		err = (*parse_clms)(*argv, ctx);
		if (err == 0) {

			argc--; argv++;
			continue;
		} else if (err == -EINVAL) {

			err = parse_precision(*argv, ctx);
			if (err == 0) {
				argc--; argv++;
			}
		}
	}
	if (ctx->help_set)
		cmd->help(cmd, ctx);
	else if (err < 0)
		handle_unknown_sarg(*argv, sargs_list_parameters);

	return err < 0 ? err : start_argc - argc;
}

int parse_map_parameters(int argc, const char *argv[], struct ibnbd_ctx *ctx)
{
	int err = 0; int start_argc = argc;
	const struct sarg *sarg;

	while (argc && err >= 0) {
		sarg = find_sarg(*argv, sargs);
		if (sarg)
			err = sarg->parse(argc, argv, 0, sarg, ctx);
		if (err == 0)
			break;

		argc--; argv++;
	}
	return err < 0 ? err : start_argc - argc;
}

int parse_unmap_parameters(int argc, const char *argv[], struct ibnbd_ctx *ctx)
{
	return parse_map_parameters(argc, argv, ctx);
}

int cmd_client_sessions(int argc, const char *argv[], struct ibnbd_ctx *ctx)
{
	static const char *_help_context = "session";

	int err = 0;
	const struct cmd *cmd;

	cmd = find_cmd(*argv, cmds_client_sessions);
	if (!cmd) {
		print_usage(_help_context, cmds_client_sessions, ctx);
		err = -EINVAL;

		if (argc)
			handle_unknown_cmd(*argv, cmds_client_sessions);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
					parse_clt_sessions_clms, cmd);
			if (err < 0)
				break;

			err = list_sessions(sess_clt, sess_clt_cnt - 1,
					    NULL, 0, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
					parse_clt_sessions_clms, cmd);
			if (err < 0)
				break;

			err = show_sessions(ctx->name, ctx);
			break;
		case TOK_RECONNECT:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			ibnbd_TODO(cmd, ctx);
			break;
		case TOK_REMAP:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			ibnbd_TODO(cmd, ctx);
			break;
		case TOK_HELP:
			print_help(_help_context, cmds_client_sessions, ctx);
			break;
		default:
			print_usage(_help_context, cmds_client_sessions, ctx);
			handle_unknown_cmd(cmd->cmd, cmds_client_sessions);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_client_devices(int argc, const char *argv[], struct ibnbd_ctx *ctx)
{
	static const char *_help_context = "device";

	int err = 0;
	const struct cmd *cmd;

	cmd = find_cmd(*argv, cmds_client_devices);
	if (!cmd) {
		print_usage(_help_context, cmds_client_devices, ctx);
		err = -EINVAL;

		if (argc)
			handle_unknown_cmd(*argv, cmds_client_devices);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
						parse_clt_devices_clms, cmd);
			if (err < 0)
				break;

			err = list_devices(sds_clt, sds_clt_cnt - 1,
					   NULL, 0, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						parse_clt_devices_clms, cmd);
			if (err < 0)
				break;

			err = show_devices(ctx->name, ctx);
			break;
		case TOK_MAP:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_from(argc--, argv++, 0, NULL, ctx);
			if (err == 0) {
				err = -EINVAL;
				break;
			}
			err = parse_map_parameters(argc, argv, ctx);
			if (err < 0)
				break;
			if (argc > 0) {

				handle_unknown_sarg(*argv, sargs);
				err = -EINVAL;
				break;
			}
			err = client_devices_map(ctx->from, ctx->name, ctx);
			break;
		/* map-by-host ?
		case TOK_MAP_BY_HOST:
			err = parse_name_help(argc--, argv++,
					_help_context, cmd, ctx);
			if (err < 0)
				break;

			if (argc > 0) {

				handle_unknown_sarg(*argv, sargs);
				err = -EINVAL;
				break;
			}
			err = cmd_client_devices(argc--, argv++, ctx);
			break;
		*/
		case TOK_RESIZE:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			if (argc > 0)
				err = parse_size(*argv);
			else
				err = -EINVAL;
			if (err < 0) {
				ERR(ctx->trm,
				    "Please provide the size of device to be configured\n");
				break;
			}
			argc--; argv++;

			/* TODO allow a unit here and take it into account */
			/* for the amount to be resized to */
			if (argc > 0) {

				handle_unknown_sarg(*argv, sargs);
				err = -EINVAL;
				break;
			}
			err = client_devices_resize(ctx->name,
						    ctx->size_sect, ctx);
			break;
		case TOK_UNMAP:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_unmap_parameters(argc, argv, ctx);
			if (err < 0)
				break;
			if (argc > 0) {

				handle_unknown_sarg(*argv, sargs);
				err = -EINVAL;
				break;
			}
			err = client_devices_unmap(ctx->name,
						   ctx->force_set, ctx);
			break;
		case TOK_REMAP:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			if (argc > 0) {

				handle_unknown_sarg(*argv, sargs);
				err = -EINVAL;
				break;
			}
			ibnbd_TODO(cmd, ctx);
			break;
		case TOK_HELP:
			print_help(_help_context, cmds_client_devices, ctx);
			break;
		default:
			print_usage(_help_context, cmds_client_devices, ctx);
			handle_unknown_cmd(cmd->cmd, cmds_client_sessions);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_client_paths(int argc, const char *argv[], struct ibnbd_ctx *ctx)
{
	static const char *_help_context = "path";

	int err = 0;
	const struct cmd *cmd;

	cmd = find_cmd(*argv, cmds_client_paths);
	if (!cmd) {
		print_usage(_help_context, cmds_client_paths, ctx);
		err = -EINVAL;

		if (argc)
			handle_unknown_cmd(*argv, cmds_client_paths);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
						    parse_clt_paths_clms, cmd);
			if (err < 0)
				break;

			err = list_paths(paths_clt, paths_clt_cnt - 1,
					 NULL, 0, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						    parse_clt_paths_clms, cmd);
			if (err < 0)
				break;

			err = show_paths(ctx->name, ctx);
			break;
		case TOK_DISCONNECT:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_name(argc--, argv++, 0, ctx);
			if (err == 0) {
				err = -EINVAL;
				break;
			}
			if (argc > 0) {

				handle_unknown_sarg(*argv, sargs);
				err = -EINVAL;
				break;
			}
			ibnbd_TODO(cmd, ctx);
			break;
		case TOK_RECONNECT:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			if (argc > 0) {

				handle_unknown_sarg(*argv, sargs);
				err = -EINVAL;
				break;
			}
			ibnbd_TODO(cmd, ctx);
			break;
		case TOK_ADD:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			if (argc > 0) {

				handle_unknown_sarg(*argv, sargs);
				err = -EINVAL;
				break;
			}
			ibnbd_TODO(cmd, ctx);
			break;
		case TOK_DELETE:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			if (argc > 0) {

				handle_unknown_sarg(*argv, sargs);
				err = -EINVAL;
				break;
			}
			ibnbd_TODO(cmd, ctx);
			break;
		case TOK_HELP:
			print_help(_help_context, cmds_client_paths, ctx);
			break;
		default:
			print_usage(_help_context, cmds_client_paths, ctx);
			handle_unknown_cmd(cmd->cmd, cmds_client_sessions);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_server_sessions(int argc, const char *argv[], struct ibnbd_ctx *ctx)
{
	static const char *_help_context = "session";

	int err = 0;
	const struct cmd *cmd;

	cmd = find_cmd(*argv, cmds_server_sessions);
	if (!cmd) {
		print_usage(_help_context, cmds_server_sessions, ctx);
		err = -EINVAL;

		if (argc)
			handle_unknown_cmd(*argv, cmds_server_sessions);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
						parse_srv_sessions_clms, cmd);
			if (err < 0)
				break;

			err = list_sessions(NULL, 0, sess_srv,
					    sess_srv_cnt - 1, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						parse_srv_sessions_clms, cmd);
			if (err < 0)
				break;

			err = show_sessions(ctx->name, ctx);
			break;
		case TOK_DISCONNECT:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			if (argc > 0) {

				handle_unknown_sarg(*argv, sargs);
				err = -EINVAL;
				break;
			}
			ibnbd_TODO(cmd, ctx);
			break;
		case TOK_HELP:
			print_help(_help_context, cmds_server_sessions, ctx);
			break;
		default:
			print_usage(_help_context, cmds_server_sessions, ctx);
			handle_unknown_cmd(cmd->cmd, cmds_client_sessions);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_server_devices(int argc, const char *argv[], struct ibnbd_ctx *ctx)
{
	static const char *_help_context = "device";

	int err = 0;
	const struct cmd *cmd;

	cmd = find_cmd(*argv, cmds_server_devices);
	if (!cmd) {
		print_usage(_help_context, cmds_server_devices, ctx);
		err = -EINVAL;

		if (argc)
			handle_unknown_cmd(*argv, cmds_server_devices);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
						parse_srv_devices_clms, cmd);
			if (err < 0)
				break;

			err = list_devices(NULL, 0, sds_srv,
					   sds_srv_cnt - 1, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						parse_srv_devices_clms, cmd);
			if (err < 0)
				break;

			err = show_devices(ctx->name, ctx);
			break;
		case TOK_HELP:
			print_help(_help_context, cmds_server_devices, ctx);
			break;
		default:
			print_usage(_help_context, cmds_server_devices, ctx);
			handle_unknown_cmd(cmd->cmd, cmds_client_sessions);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_server_paths(int argc, const char *argv[], struct ibnbd_ctx *ctx)
{
	static const char *_help_context = "path";

	int err = 0;
	const struct cmd *cmd;

	cmd = find_cmd(*argv, cmds_server_paths);
	if (!cmd) {
		print_usage(_help_context, cmds_server_paths, ctx);
		err = -EINVAL;

		if (argc)
			handle_unknown_cmd(*argv, cmds_server_paths);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:
			err = parse_list_parameters(argc, argv, ctx,
						parse_srv_paths_clms, cmd);
			if (err < 0)
				break;

			err = list_paths(NULL, 0, paths_srv,
					 paths_srv_cnt - 1, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						parse_srv_devices_clms, cmd);
			if (err < 0)
				break;

			err = cmd_server_paths(argc--, argv++, ctx);
			break;
		case TOK_DISCONNECT:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			if (argc > 0) {

				handle_unknown_sarg(*argv, sargs);
				err = -EINVAL;
				break;
			}
			ibnbd_TODO(cmd, ctx);
			break;
		case TOK_HELP:
			print_help(_help_context, cmds_server_paths, ctx);
			break;
		default:
			print_usage(_help_context, cmds_server_paths, ctx);
			handle_unknown_cmd(cmd->cmd, cmds_client_sessions);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int main(int argc, const char *argv[])
{
	int ret = 0;
#if 0
	int i, rc_cd, rc_cs, rc_cp, rc_sd, rc_ss, rc_sp;
	const struct sarg *sarg;
	const struct cmd *cmd;
#endif

	ctx.trm = (isatty(STDOUT_FILENO) == 1);

	init_ibnbd_ctx(&ctx);

	if (parse_argv0(argv[0], &ctx)) {

		if (argc < 2) {
			ERR(ctx.trm, "no object type specified\n");
			usage_sarg(argv[0],
				   sargs_object_type_help_client, &ctx);
			ret = -EINVAL;
			goto out;
		}

		ret = ibnbd_sysfs_alloc_all(&sds_clt, &sds_srv,
					    &sess_clt, &sess_srv,
					    &paths_clt, &paths_srv,
					    &sds_clt_cnt, &sds_srv_cnt,
					    &sess_clt_cnt, &sess_srv_cnt,
					    &paths_clt_cnt, &paths_srv_cnt);
		if (ret) {
			ERR(ctx.trm,
			    "Failed to alloc memory for sysfs entries: %d\n",
			    ret);
			goto out;
		}
		ret = ibnbd_sysfs_read_all(sds_clt, sds_srv, sess_clt, sess_srv,
					   paths_clt, paths_srv);
		if (ret) {
			ERR(ctx.trm, "Failed to read sysfs entries: %d\n", ret);
			goto free;
		}
		ibnbd_ctx_default(&ctx);

		argv++; argc--;

		switch (ctx.ibnbdmode) {
		case IBNBD_CLIENT:
			ret = cmd_client(argc, argv, &ctx);
			break;
		case IBNBD_SERVER:
			ret = cmd_server(argc, argv, &ctx);
			break;
		default:
			ERR(ctx.trm,
			    "either client or server mode have to be specified\n");
			print_usage(NULL, cmds, &ctx);
			ret = -EINVAL;
			break;
		}
		goto free;
	}

#if 0
	if (argc < 2) {
		ERR(ctx.trm, "no command specified\n");
		print_help(argv[0], cmds, &ctx);
		ret = -EINVAL;
		goto out;
	}

	i = 1;

	/*
	 * try finding sess/devs/paths preceding the command
	 * (for those who is used to type ibnbd dev map or ibnbd session list)
	 */
	i = parse_lst(argc, argv, i, NULL, &ctx);
	/*
	 * try finding clt/srv preceding the command
	 * (for those who is used to type ibnbd clt list or ibnbd srv sess list)
	 */
	i = parse_mode(argc, argv, i, NULL, &ctx);

	cmd = find_cmd(argv[i], cmds);
	if (!cmd) {
		printf("'%s' is not a valid command. Try '%s%s%s %s%s%s'\n",
		       argv[i], CLR(ctx.trm, CBLD, argv[0]),
		       CLR(ctx.trm, CBLD, "help"));
		handle_unknown_cmd(argv[i], cmds);
		ret = -EINVAL;
		goto out;
	}
	if (cmd == find_cmd("help", cmds))
		ctx.help_set = true;

	if (i + 1 < argc && cmd->help &&
	    (!strcmp(argv[i + 1], "help") ||
	     !strcmp(argv[i + 1], "--help") ||
	     !strcmp(argv[i + 1], "-h"))) {
		cmd->help(cmd, &ctx);
		goto out;
	}

	if (cmd->parse_args) {
		ret = cmd->parse_args(argc, argv, i, &ctx);
		if (ret == i) {
			if (cmd->help)
				cmd->help(cmd, &ctx);
			ret = -EINVAL;
			goto out;
		}
		i = ret;
	} else {
		i++;
	}

	while (i < argc) {
		sarg = find_sarg(argv[i], sargs);
		if (!sarg) {
			rc_cd = parse_clt_devices_clms(argv[i], &ctx);
			rc_sd = parse_srv_devices_clms(argv[i], &ctx);
			rc_cs = parse_clt_sessions_clms(argv[i], &ctx);
			rc_ss = parse_srv_sessions_clms(argv[i], &ctx);
			rc_cp = parse_clt_paths_clms(argv[i], &ctx);
			rc_sp = parse_srv_paths_clms(argv[i], &ctx);
			if (!parse_precision(argv[i], &ctx) ||
			    !(rc_cd && rc_cs && rc_cp && rc_sd && rc_ss && rc_sp) ||
			    !parse_path(argv[i]) ||
			    !parse_size(argv[i])) {
				i++;
				continue;
			}

			printf("'%s' is not a valid argument. Try '", argv[i]);
			printf("%s%s%s %s%s%s %s%s%s",
			       CLR(ctx.trm, CBLD, ctx.pname),
			       CLR(ctx.trm, CBLD, cmd->cmd),
			       CLR(ctx.trm, CBLD, "help"));
			printf("'\n");

			handle_unknown_sarg(argv[i], sargs);

			ret = -EINVAL;
			goto out;
		}
		ret = sarg->parse(argc, argv, i, sarg, &ctx);
		if (i == ret) {
			ret = -EINVAL;
			goto out;
		}
		i = ret;
	}

#endif

	ret = ibnbd_sysfs_alloc_all(&sds_clt, &sds_srv,
				    &sess_clt, &sess_srv,
				    &paths_clt, &paths_srv,
				    &sds_clt_cnt, &sds_srv_cnt,
				    &sess_clt_cnt, &sess_srv_cnt,
				    &paths_clt_cnt, &paths_srv_cnt);
	if (ret) {
		ERR(ctx.trm,
		    "Failed to alloc memory for sysfs entries: %d\n", ret);
		goto out;
	}

	ret = ibnbd_sysfs_read_all(sds_clt, sds_srv, sess_clt, sess_srv,
				   paths_clt, paths_srv);
	if (ret) {
		ERR(ctx.trm, "Failed to read sysfs entries: %d\n", ret);
		goto free;
	}

	ibnbd_ctx_default(&ctx);

#if 0
	if (ctx.help_set && cmd->help)
		cmd->help(cmd, &ctx);
	else if (cmd->func) {
		/*
		 * if (args.ibnbdmode == IBNBD_NONE) {
		 *	ERR("ibnbd modules not loaded\n");
		 *	ret = -ENOENT;
		 *	goto free;
		 * }
		 */
		ret = cmd->func();
	}
#else

	ret = cmd_start(--argc, ++argv, &ctx);

#endif

free:
	ibnbd_sysfs_free_all(sds_clt, sds_srv, sess_clt, sess_srv,
			     paths_clt, paths_srv);
out:
	return ret;
}
