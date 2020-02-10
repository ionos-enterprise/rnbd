// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Configuration tool for RNBD driver and RTRS library.
 *
 * Copyright (c) 2019 1&1 IONOS SE. All rights reserved.
 * Authors: Danil Kipnis <danil.kipnis@cloud.ionos.com>
 *          Lutz Pogrell <lutz.pogrell@cloud.ionos.com>
 */

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

#include "rnbd-sysfs.h"
#include "rnbd-clms.h"

#define INF(verbose_set, fmt, ...)		\
	do { \
		if (verbose_set) \
			printf(fmt, ##__VA_ARGS__); \
	} while (0)

static struct rnbd_sess_dev **sds_clt;
static struct rnbd_sess_dev **sds_srv;
static struct rnbd_sess **sess_clt;
static struct rnbd_sess **sess_srv;
static struct rnbd_path **paths_clt;
static struct rnbd_path **paths_srv;
static int sds_clt_cnt, sds_srv_cnt,
	   sess_clt_cnt, sess_srv_cnt,
	   paths_clt_cnt, paths_srv_cnt;

struct param {
	enum rnbd_token tok;
	const char *param_str;
	const char *short_d;
	const char *short_d2;
	const char *descr;
	const char *params;
	int (*parse)(int argc, const char *argv[],
		     const struct param *param, struct rnbd_ctx *ctx);
	void (*help)(const char *program_name,
		     const struct param *cmd,
		     const struct rnbd_ctx *ctx);
	size_t offset;
	int dist;
};

static int parse_fmt(int argc, const char *argv[],
		     const struct param *param, struct rnbd_ctx *ctx)
{
	if (!strcasecmp(*argv, "csv"))
		ctx->fmt = FMT_CSV;
	else if (!strcasecmp(*argv, "json"))
		ctx->fmt = FMT_JSON;
	else if (!strcasecmp(*argv, "xml"))
		ctx->fmt = FMT_XML;
	else if (!strcasecmp(*argv, "term"))
		ctx->fmt = FMT_TERM;
	else
		return 0;

	ctx->fmt_set = true;

	return 1;
}

enum lstmode {
	LST_DEVICES,
	LST_SESSIONS,
	LST_PATHS
};

static int parse_lst(int argc, const char *argv[],
		     const struct param *param, struct rnbd_ctx *ctx)
{
	if (!strcasecmp(*argv, "devices") ||
	    !strcasecmp(*argv, "device") ||
	    !strcasecmp(*argv, "devs") ||
	    !strcasecmp(*argv, "dev"))
		ctx->lstmode = LST_DEVICES;
	else if (!strcasecmp(*argv, "sessions") ||
		 !strcasecmp(*argv, "session") ||
		 !strcasecmp(*argv, "sess"))
		ctx->lstmode = LST_SESSIONS;
	else if (!strcasecmp(*argv, "paths") ||
		 !strcasecmp(*argv, "path"))
		ctx->lstmode = LST_PATHS;
	else
		return 0;

	ctx->lstmode_set = true;

	return 1;
}

static int parse_from(int argc, const char *argv[],
		      const struct param *param, struct rnbd_ctx *ctx)
{
	if (argc < 2) {
		ERR(ctx->trm, "Please specify the destination to map from\n");
		return 0;
	}

	ctx->from = argv[1];
	ctx->from_set = 1;

	return 2;
}

static int parse_help(int argc, const char *argv[],
		      const struct param *param, struct rnbd_ctx *ctx)
{
	ctx->help_set = true;

	if (argc > 1) {

		ctx->help_arg = argv[1];
		ctx->help_arg_set = 1;
	}
	return 1;
}

static void parse_argv0(const char *argv0, struct rnbd_ctx *ctx)
{
	const char *prog_name = strrchr(argv0, '/');

	if (!prog_name)
		prog_name = argv0;
	else
		prog_name++;

	ctx->pname = prog_name;
}

static int parse_mode(int argc, const char *argv[],
		     const struct param *param, struct rnbd_ctx *ctx)
{
	if (!strcasecmp(*argv, "client") || !strcasecmp(*argv, "clt"))
		ctx->rnbdmode = RNBD_CLIENT;
	else if (!strcasecmp(*argv, "server") || !strcasecmp(*argv, "srv"))
		ctx->rnbdmode = RNBD_SERVER;
	else if (!strcasecmp(*argv, "both"))
		ctx->rnbdmode = RNBD_BOTH;
	else
		return 0;

	ctx->rnbdmode_set = true;

	return 1;
}

static int parse_rw(int argc, const char *argv[],
		     const struct param *param, struct rnbd_ctx *ctx)
{
	if (strcasecmp(*argv, "ro") &&
	    strcasecmp(*argv, "rw") &&
	    strcasecmp(*argv, "migration"))
		return 0;

	ctx->access_mode = *argv;
	ctx->access_mode_set = true;

	return 1;
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

static int parse_apply_unit(const char *str, struct rnbd_ctx *ctx)
{
	int rc, shift;

	if (!ctx->size_set)
		return -EINVAL;

	rc = get_unit_shift(str, &shift);
	if (rc < 0)
		return rc;

	ctx->size_sect <<= shift;
	INF(ctx->debug_set,
	    "unit %s accepted left shift size_sect by %d.\n",
	    str, shift);

	return 0;
}

static int parse_unit(int argc, const char *argv[],
		      const struct param *param, struct rnbd_ctx *ctx)
{
	int rc;

	rc = get_unit_index(param->param_str, &ctx->unit_id);
	if (rc < 0)
		return 0;

	clm_set_hdr_unit(&clm_rnbd_dev_rx_sect, param->descr);
	clm_set_hdr_unit(&clm_rnbd_dev_tx_sect, param->descr);
	clm_set_hdr_unit(&clm_rnbd_sess_rx_bytes, param->descr);
	clm_set_hdr_unit(&clm_rnbd_sess_tx_bytes, param->descr);
	clm_set_hdr_unit(&clm_rnbd_path_rx_bytes, param->descr);
	clm_set_hdr_unit(&clm_rnbd_path_tx_bytes, param->descr);

	ctx->unit_set = true;
	return 1;
}

static int parse_all(int argc, const char *argv[],
		     const struct param *param, struct rnbd_ctx *ctx)
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

	return 1;
}

static int parse_flag(int argc, const char *argv[],
		     const struct param *param, struct rnbd_ctx *ctx)
{
	*(short *)(((char *)ctx)+(param->offset)) = 1;

	return 1;
}

static int parse_debug(int argc, const char *argv[],
		     const struct param *param, struct rnbd_ctx *ctx)
{
	ctx->debug_set = true;
	ctx->verbose_set = true;

	return 1;
}

static struct param _params_from =
	{TOK_FROM, "from", "", "", "Destination to map a device from",
	 NULL, parse_from, 0};
static struct param _params_client =
	{TOK_CLIENT, "client", "", "", "Operations of client",
	 NULL, parse_mode, 0};
static struct param _params_clt =
	{TOK_CLIENT, "clt", "", "", "Operations of client",
	 NULL, parse_mode, 0};
static struct param _params_cli =
	{TOK_CLIENT, "cli", "", "", "Operations of client",
	 NULL, parse_mode, 0};
static struct param _params_server =
	{TOK_SERVER, "server", "", "", "Operations of server",
	 NULL, parse_mode, 0};
static struct param _params_serv =
	{TOK_SERVER, "serv", "", "", "Operations of server",
	 NULL, parse_mode, 0};
static struct param _params_srv =
	{TOK_SERVER, "srv", "", "", "Operations of server",
	 NULL, parse_mode, 0};
static struct param _params_both =
	{TOK_BOTH, "both", "", "", "Operations of both client and server",
	 NULL, parse_mode, 0};
static struct param _params_devices_client =
	{TOK_DEVICES, "devices", "", "", "Map/unmapped/modify devices",
	 NULL, parse_lst, 0};
static struct param _params_devices =
	{TOK_DEVICES, "devices", "", "", "List devices", NULL, parse_lst, 0};
static struct param _params_device =
	{TOK_DEVICES, "device", "", "", "", NULL, parse_lst, 0};
static struct param _params_devs =
	{TOK_DEVICES, "devs", "", "", "", NULL, parse_lst, 0};
static struct param _params_dev =
	{TOK_DEVICES, "dev", "", "", "", NULL, parse_lst, 0};
static struct param _params_sessions =
	{TOK_SESSIONS, "sessions", "", "", "Operate on sessions",
	 NULL, parse_lst, 0};
static struct param _params_session =
	{TOK_SESSIONS, "session", "", "", "", NULL, parse_lst, 0};
static struct param _params_sess =
	{TOK_SESSIONS, "sess", "", "", "", NULL, parse_lst, 0};
static struct param _params_paths =
	{TOK_PATHS, "paths", "", "", "Handle paths", NULL, parse_lst, 0};
static struct param _params_path =
	{TOK_PATHS, "path", "", "", "", NULL, parse_lst, 0};
static struct param _params_path_param =
	{TOK_PATHS, "<path>", "", "",
	 "Path to use (i.e. gid:fe80::1@gid:fe80::2)",
	 NULL, NULL, 0};
static struct param _params_notree =
	{TOK_NOTREE, "notree", "", "", "Don't display paths for each sessions",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, notree_set)};
static struct param _params_xml =
	{TOK_XML, "xml", "", "", "Print in XML format", NULL, parse_fmt, 0};
static struct param _params_cvs =
	{TOK_CSV, "csv", "", "", "Print in CSV format", NULL, parse_fmt, 0};
static struct param _params_json =
	{TOK_JSON, "json", "", "", "Print in JSON format", NULL, parse_fmt, 0};
static struct param _params_term =
	{TOK_TERM, "term", "", "", "Print for terminal", NULL, parse_fmt, 0};
static struct param _params_ro =
	{TOK_RO, "ro", "", "", "Readonly", NULL, parse_rw, 0};
static struct param _params_rw =
	{TOK_RW, "rw", "", "", "Writable", NULL, parse_rw, 0};
static struct param _params_migration =
	{TOK_MIGRATION, "migration", "", "", "Writable (migration)",
	 NULL, parse_rw, 0};
static struct param _params_help =
	{TOK_HELP, "help", "", "", "Display help and exit",
	 NULL, parse_help, NULL, offsetof(struct rnbd_ctx, help_set)};
static struct param _params_version =
	{TOK_VERSION, "version", "", "", "Display version of this tool and exit",
	 NULL, NULL, NULL, 0};
static struct param _params_verbose =
	{TOK_VERBOSE, "verbose", "", "", "Verbose output",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, verbose_set)};
static struct param _params_minus_h =
	{TOK_VERBOSE, "-h", "", "", "Help output",
	 NULL, parse_help, NULL, offsetof(struct rnbd_ctx, help_set)};
static struct param _params_minus_minus_help =
	{TOK_VERBOSE, "--help", "", "", "Help output",
	 NULL, parse_help, NULL, offsetof(struct rnbd_ctx, help_set)};
static struct param _params_minus_v =
	{TOK_VERBOSE, "-v", "", "", "Verbose output",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, verbose_set)};
static struct param _params_minus_minus_verbose =
	{TOK_VERBOSE, "--verbose", "", "", "Verbose output",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, verbose_set)};
static struct param _params_minus_d =
	{TOK_VERBOSE, "-d", "", "", "Debug mode",
	 NULL, parse_debug, NULL, offsetof(struct rnbd_ctx, debug_set)};
static struct param _params_minus_minus_debug =
	{TOK_VERBOSE, "--debug", "", "", "Debug mode", NULL, parse_debug, 0};
static struct param _params_minus_s =
	{TOK_VERBOSE, "-s", "", "", "Simulate",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, simulate_set)};
static struct param _params_minus_minus_simulate =
	{TOK_VERBOSE, "--simulate", "", "",
	 "Only print modifying operations, do not execute",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, simulate_set)};
static struct param _params_minus_c =
	{TOK_VERBOSE, "-c", "", "", "Complete",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, complete_set)};
static struct param _params_minus_minus_complete =
	{TOK_VERBOSE, "--complete", "", "", "Complete",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, complete_set)};
static struct param _params_byte =
	{TOK_BYTE, "B", "", "", "Byte", NULL, parse_unit, 0};
static struct param _params_kib =
	{TOK_KIB, "K", "", "", "KiB", NULL, parse_unit, 0};
static struct param _params_mib =
	{TOK_MIB, "M", "", "", "MiB", NULL, parse_unit, 0};
static struct param _params_gib =
	{TOK_GIB, "G", "", "", "GiB", NULL, parse_unit, 0};
static struct param _params_tib =
	{TOK_TIB, "T", "", "", "TiB", NULL, parse_unit, 0};
static struct param _params_pib =
	{TOK_PIB, "P", "", "", "PiB", NULL, parse_unit, 0};
static struct param _params_eib =
	{TOK_EIB, "E", "", "", "EiB", NULL, parse_unit, 0};
static struct param _params_noheaders =
	{TOK_NOHEADERS, "noheaders", "", "", "Don't print headers",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, noheaders_set)};
static struct param _params_nototals =
	{TOK_NOTOTALS, "nototals", "", "", "Don't print totals",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, nototals_set)};
static struct param _params_force =
	{TOK_FORCE, "force", "", "", "Force operation",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, force_set)};
static struct param _params_noterm =
	{TOK_NOTERM, "noterm", "", "", "Non-interactive mode",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, noterm_set)};
#if 0
static struct param _params_minus_f =
	{TOK_FORCE, "-f", "", "", "",
	 NULL, parse_flag, NULL, offsetof(struct rnbd_ctx, force_set)};
#endif
static struct param _params_all =
	{TOK_ALL, "all", "", "", "Print all columns", NULL, parse_all, 0};
static struct param _params_null =
	{TOK_NONE, 0};

static struct param *params_options[] = {
	&_params_noheaders,
	&_params_nototals,
	&_params_notree,
	&_params_force,
	&_params_help,
	&_params_verbose,
	&_params_null
};

static const struct param *find_param(const char *str,
				    struct param *const params[])
{
	if (str) {
		do {
			if (!strcasecmp(str, (*params)->param_str))
				return *params;
		} while ((*++params)->param_str);
	}
	return NULL;
}

static  void usage_param(const char *str, struct param *const params[],
			const struct rnbd_ctx *ctx)
{
	if (ctx->complete_set) {
		for (; (*params)->param_str; params++)
			printf("%s ", (*params)->param_str);
		printf("\n");
	} else {
		printf("Usage: %s%s%s ", CLR(ctx->trm, CBLD, str));

		clr_print(ctx->trm, CBLD, "%s", (*params)->param_str);

		while ((*++params)->param_str)
			printf("|%s%s%s",
			       CLR(ctx->trm, CBLD, (*params)->param_str));

		printf(" ...\n\n");
	}
}

#define HP "    "
#define HPRE HP "                "
#define HOPT HP "%-16s%s\n"

static void print_opt(const char *opt, const char *descr)
{
	printf(HOPT, opt, descr);
}

static void print_param_descr(char *str)
{
	const struct param *s;

	s = find_param(str, params_options);
	if (s)
		print_opt(s->param_str, s->descr);
}

static  void print_param(const char *str, struct param *const params[],
			const struct rnbd_ctx *ctx)
{
	do {
		print_opt((*params)->param_str, (*params)->descr);
	} while ((*++params)->param_str);
}

static  void help_param(const char *str, struct param *const params[],
		       const struct rnbd_ctx *ctx)
{
	usage_param(str, params, ctx);

	print_param(str, params, ctx);
}

static void print_usage(const char *sub_name, struct param * const cmds[],
			const struct rnbd_ctx *ctx)
{
	if (ctx->complete_set) {
		for (; (*cmds)->param_str; cmds++)
			printf("%s ", (*cmds)->param_str);
		printf("\n");
	} else {
		if (sub_name)
			printf("Usage: %s%s%s %s {",
			       CLR(ctx->trm, CBLD, ctx->pname), sub_name);
		else
			printf("Usage: %s%s%s {",
			       CLR(ctx->trm, CBLD, ctx->pname));

		clr_print(ctx->trm, CBLD, "%s", (*cmds)->param_str);

		while ((*++cmds)->param_str)
			printf("|%s%s%s",
			       CLR(ctx->trm, CBLD, (*cmds)->param_str));

		printf("} [ARGUMENTS]\n\n");
	}
}

static bool help_print_all(const struct rnbd_ctx *ctx)
{
	if (ctx->help_arg_set && strncmp(ctx->help_arg, "all", 3) == 0)
		return true;
	else
		return false;
}

static bool help_print_flags(const struct rnbd_ctx *ctx)
{
	if (ctx->help_arg_set && strncmp(ctx->help_arg, "flags", 3) == 0)
		return true;
	else
		return false;
}

static bool help_print_fields(const struct rnbd_ctx *ctx)
{
	if (ctx->help_arg_set && strncmp(ctx->help_arg, "fields", 4) == 0)
		return true;
	else
		return false;
}

static void cmd_print_usage_short(const struct param *cmd, const char *a,
			    const struct rnbd_ctx *ctx)
{
	if (a && *a)
		printf("Usage: %s%s%s %s%s%s %s%s%s",
		       CLR(ctx->trm, CBLD, ctx->pname),
		       CLR(ctx->trm, CBLD, a),
		       CLR(ctx->trm, CBLD, cmd->param_str));
	else
		printf("Usage: %s%s%s %s%s%s",
		       CLR(ctx->trm, CBLD, ctx->pname),
		       CLR(ctx->trm, CBLD, cmd->param_str));

	if (cmd->params)
		printf(" %s", cmd->params);
	printf(" [OPTIONS]\n\n");
}

static void cmd_print_usage_descr(const struct param *cmd, const char *a,
				  const struct rnbd_ctx *ctx)
{
	cmd_print_usage_short(cmd, a, ctx);
	printf("%s\n", cmd->descr);
}
static void print_help(const char *program_name,
		       const struct param *cmd,
		       struct param *const sub_cmds[],
		       const struct rnbd_ctx *ctx)
{
	print_usage(program_name, sub_cmds, ctx);

	cmd->help(program_name, cmd, ctx);

	printf("\nSubcommands:\n");
	do {
		if (help_print_all(ctx)) {
			printf("\n\n");
			(*sub_cmds)->help(program_name, *sub_cmds, ctx);
		} else {
			if (program_name)
				printf("     %-*s%s %s%s\n", 20,
				       (*sub_cmds)->param_str,
				       (*sub_cmds)->short_d,
				       program_name,
				       (*sub_cmds)->short_d2);
			else
				printf("     %-*s%s\n", 20,
				       (*sub_cmds)->param_str,
				       (*sub_cmds)->short_d);
		}
	} while ((*++sub_cmds)->param_str);
}

static void print_clms_list(struct table_column **clms)
{
	if (*clms)
		printf("%s", (*clms)->m_name);

	while (*++clms)
		printf(",%s", (*clms)->m_name);

	printf("\n");
}

static void help_object(const char *program_name,
			const struct param *cmd,
			const struct rnbd_ctx *ctx)
{
	const char *word;

	word = strchr(program_name, ' ');
	if (word)
		word++; /* skip space */
	else
		word = program_name;

	printf(HP "Execute operations on %ss.\n", word);
}

static void help_fields(void)
{
	print_opt("{fields}",
		  "Comma separated list of fields to be printed. The list can be");
	print_opt("",
		  "prefixed with '+' or '-' to add or remove fields from the ");
	print_opt("", "default selection.\n");
}

static void print_fields(const struct rnbd_ctx *ctx,
			 struct table_column **def_clt,
			 struct table_column **def_srv,
			 struct table_column **all,
			 enum rnbdmode mode)
{
	table_tbl_print_term(HPRE, all, ctx->trm, ctx);
	if (mode != RNBD_SERVER) {
		printf("\n%sDefault%s: ",
		       HPRE, mode == RNBD_BOTH ? " client" : "");
		print_clms_list(def_clt);
	}
	if (mode != RNBD_CLIENT) {
		printf("%sDefault%s: ",
		       HPRE, mode == RNBD_BOTH ? " server" : "");
		print_clms_list(def_srv);
	}
	printf("\n");
}

static void help_dump_all(const char *program_name,
			  const struct param *cmd,
			  const struct rnbd_ctx *ctx)
{
	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nOptions:\n");
	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		printf("%s%s%s%s\n", HPRE,
		       CLR(ctx->trm, CDIM, "Device Fields"));
		print_fields(ctx, def_clms_devices_clt,
			     def_clms_devices_srv,
			     all_clms_devices, RNBD_BOTH);

		printf("%s%s%s%s\n", HPRE,
		       CLR(ctx->trm, CDIM, "Session Fields"));
		print_fields(ctx, def_clms_sessions_clt,
			     def_clms_sessions_srv,
			     all_clms_sessions, RNBD_BOTH);

		printf("%s%s%s%s\n", HPRE,
		       CLR(ctx->trm, CDIM, "Path Fields"));
		print_fields(ctx, def_clms_paths_clt,
			     def_clms_paths_srv,
			     all_clms_paths, RNBD_BOTH);

		if (help_print_fields(ctx))
			return;
	}

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_param_descr("notree");
	print_param_descr("noheaders");
	print_param_descr("nototals");
	print_param_descr("help");
}

static void help_list_devices(const char *program_name,
			      const struct param *cmd,
			      const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "devices";

	cmd_print_usage_descr(cmd, program_name, ctx);

	if (!help_print_fields(ctx))
		printf("\nOptions:\n");

	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(ctx, def_clms_devices_clt, def_clms_devices_srv,
			     all_clms_devices, ctx->rnbdmode);

		if (help_print_fields(ctx))
			return;
	}
	if (!help_print_all(ctx))
		printf("%sProvide 'all' to print all available fields\n\n",
		       HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_param_descr("notree");
	print_param_descr("noheaders");
	print_param_descr("nototals");
	print_opt("help", "Display help and exit. [fields|all]");
}

static void help_list_sessions(const char *program_name,
			       const struct param *cmd,
			       const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "sessions";

	cmd_print_usage_descr(cmd, program_name, ctx);

	if (!help_print_fields(ctx))
		printf("\nOptions:\n");

	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(ctx, def_clms_sessions_clt, def_clms_sessions_srv,
			     all_clms_sessions, ctx->rnbdmode);

		if (help_print_fields(ctx))
			return;
	}

	if (!help_print_all(ctx))
		printf("%sProvide 'all' to print all available fields\n\n",
		       HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_param_descr("notree");
	print_param_descr("noheaders");
	print_param_descr("nototals");
	print_opt("help", "Display help and exit. [fields|all]");
}

static void help_list_paths(const char *program_name,
			    const struct param *cmd,
			    const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "paths";

	cmd_print_usage_descr(cmd, program_name, ctx);

	if (!help_print_fields(ctx))
		printf("\nOptions:\n");

	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(ctx, def_clms_paths_clt, def_clms_paths_srv,
			     all_clms_paths, ctx->rnbdmode);

		if (help_print_fields(ctx))
			return;
	}

	if (!help_print_all(ctx))
		printf("%sProvide 'all' to print all available fields\n\n",
		       HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_param_descr("notree");
	print_param_descr("noheaders");
	print_param_descr("nototals");
	print_opt("help", "Display help and exit. [fields|all]");
}

static int list_devices(struct rnbd_sess_dev **d_clt, int d_clt_cnt,
			struct rnbd_sess_dev **d_srv, int d_srv_cnt,
			bool is_dump, struct rnbd_ctx *ctx)
{
	if (!(ctx->rnbdmode & RNBD_CLIENT))
		d_clt_cnt = 0;
	if (!(ctx->rnbdmode & RNBD_SERVER))
		d_srv_cnt = 0;

	switch (ctx->fmt) {
	case FMT_CSV:
		if ((d_clt_cnt && d_srv_cnt) || ctx->rnbdmode == RNBD_BOTH)
			printf("Imports:\n");

		if (d_clt_cnt)
			list_devices_csv(d_clt, ctx->clms_devices_clt, ctx);

		if ((d_clt_cnt && d_srv_cnt) || ctx->rnbdmode == RNBD_BOTH)
			printf("Exports:\n");

		if (d_srv_cnt)
			list_devices_csv(d_srv, ctx->clms_devices_srv, ctx);

		break;
	case FMT_JSON:
		printf("{\n");

		if ((d_clt_cnt && d_srv_cnt) || ctx->rnbdmode == RNBD_BOTH)
			printf("\t\"imports\": ");

		if (d_clt_cnt)
			list_devices_json(d_clt, ctx->clms_devices_clt, ctx);

		if (d_clt_cnt && d_srv_cnt)
			printf(",\n");
		else if (!d_clt_cnt && ctx->rnbdmode == RNBD_BOTH)
			printf("null,\n");
		else
			printf(",\n");

		if ((d_clt_cnt && d_srv_cnt) || ctx->rnbdmode == RNBD_BOTH)
			printf("\t\"exports\": ");

		if (d_srv_cnt)
			list_devices_json(d_srv, ctx->clms_devices_srv, ctx);
		else if (ctx->rnbdmode == RNBD_BOTH)
			printf("null");

		if (!is_dump)
			printf("\n}\n");
		else
			printf(",\n");

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
		if ((d_clt_cnt && d_srv_cnt && !ctx->noheaders_set)
		    || (d_clt_cnt && is_dump))
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Imported devices"));

		if (d_clt_cnt)
			list_devices_term(d_clt, ctx->clms_devices_clt, ctx);

		if (d_clt_cnt && d_srv_cnt && is_dump)
			printf("\n");

		if ((d_clt_cnt && d_srv_cnt && !ctx->noheaders_set)
		    || (d_srv_cnt && is_dump))
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Exported devices"));

		if (d_srv_cnt)
			list_devices_term(d_srv, ctx->clms_devices_srv, ctx);

		break;
	}
	return 0;
}

static int list_sessions(struct rnbd_sess **s_clt, int clt_s_num,
			 struct rnbd_sess **s_srv, int srv_s_num,
			 bool is_dump, struct rnbd_ctx *ctx)
{
	if (!(ctx->rnbdmode & RNBD_CLIENT))
		clt_s_num = 0;
	if (!(ctx->rnbdmode & RNBD_SERVER))
		srv_s_num = 0;

	switch (ctx->fmt) {
	case FMT_CSV:
		if (clt_s_num && srv_s_num)
			printf("Outgoing sessions:\n");

		if (clt_s_num)
			list_sessions_csv(s_clt, ctx->clms_sessions_clt, ctx);

		if (clt_s_num && srv_s_num)
			printf("Incoming sessions:\n");

		if (srv_s_num)
			list_sessions_csv(s_srv, ctx->clms_sessions_srv, ctx);
		break;
	case FMT_JSON:
		if (!is_dump)
			printf("{\n");

		printf("\t\"outgoing sessions\": ");
		if (clt_s_num)
			list_sessions_json(s_clt, ctx->clms_sessions_clt, ctx);
		else if (ctx->rnbdmode == RNBD_BOTH)
			printf("null");

		if ((clt_s_num && srv_s_num) || ctx->rnbdmode == RNBD_BOTH)
			printf(",");

		printf("\n");

		printf("\t\"incoming sessions\": ");
		if (srv_s_num)
			list_sessions_json(s_srv, ctx->clms_sessions_srv, ctx);
		else if (ctx->rnbdmode == RNBD_BOTH)
			printf("null");

		if (!is_dump)
			printf("\n}\n");
		else
			printf(",\n");

		break;
	case FMT_XML:
		if (clt_s_num) {
			printf("<outgoing-sessions>\n");
			list_sessions_xml(s_clt, ctx->clms_sessions_clt, ctx);
			printf("</outgoing-sessions>\n");
		}
		if (srv_s_num) {
			printf("<incoming-sessions>\n");
			list_sessions_xml(s_srv, ctx->clms_sessions_srv, ctx);
			printf("</incoming-sessions>\n");
		}

		break;
	case FMT_TERM:
	default:
		if ((clt_s_num && srv_s_num && !ctx->noheaders_set)
		    || (clt_s_num && is_dump))
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Outgoing sessions"));

		if (clt_s_num)
			list_sessions_term(s_clt, ctx->clms_sessions_clt, ctx);

		if (clt_s_num && srv_s_num && is_dump)
			printf("\n");

		if ((clt_s_num && srv_s_num && !ctx->noheaders_set)
		    || (srv_s_num && is_dump))
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Incoming sessions"));

		if (srv_s_num)
			list_sessions_term(s_srv, ctx->clms_sessions_srv, ctx);
		break;
	}

	return 0;
}

static int list_paths(struct rnbd_path **p_clt, int clt_p_num,
		      struct rnbd_path **p_srv, int srv_p_num,
		      bool is_dump, struct rnbd_ctx *ctx)
{
	if (!(ctx->rnbdmode & RNBD_CLIENT))
		clt_p_num = 0;
	if (!(ctx->rnbdmode & RNBD_SERVER))
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
		if (!is_dump)
			printf("{\n");

		printf("\t\"outgoing paths\": ");
		if (clt_p_num)
			list_paths_json(p_clt, ctx->clms_paths_clt, ctx);
		else if (ctx->rnbdmode == RNBD_BOTH)
			printf("null");

		if ((clt_p_num && srv_p_num) || ctx->rnbdmode == RNBD_BOTH)
			printf(",");

		printf("\n");

		printf("\t\"incoming paths\": ");
		if (srv_p_num)
			list_paths_json(p_srv, ctx->clms_paths_srv, ctx);
		else if (ctx->rnbdmode == RNBD_BOTH)
			printf("null");

		printf("\n}\n");

		break;
	case FMT_XML:
		if (clt_p_num) {
			printf("<outgoing-paths>\n");
			list_paths_xml(p_clt, ctx->clms_paths_clt, ctx);
			printf("</outgoing-paths>\n");
		}
		if (srv_p_num) {
			printf("<incoming-paths>\n");
			list_paths_xml(p_srv, ctx->clms_paths_srv, ctx);
			printf("</incoming-paths>\n");
		}

		break;
	case FMT_TERM:
	default:
		if ((clt_p_num && srv_p_num && !ctx->noheaders_set)
		    || (clt_p_num && is_dump))
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Outgoing paths"));

		if (clt_p_num)
			list_paths_term(p_clt, clt_p_num,
					ctx->clms_paths_clt, 0, ctx);

		if (clt_p_num && srv_p_num && is_dump)
			printf("\n");

		if ((clt_p_num && srv_p_num && !ctx->noheaders_set)
		    || (srv_p_num && is_dump))
			printf("%s%s%s\n",
			       CLR(ctx->trm, CDIM, "Incoming paths"));

		if (srv_p_num)
			list_paths_term(p_srv, srv_p_num,
					ctx->clms_paths_srv, 0, ctx);
		break;
	}

	return 0;
}

static bool match_device(struct rnbd_sess_dev *d, const char *name)
{
	if (!strcmp(d->mapping_path, name) ||
	    !strcmp(d->dev->devname, name) ||
	    !strcmp(d->dev->devpath, name))
		return true;

	return false;
}

static int find_devices(const char *name, struct rnbd_sess_dev **devs,
			struct rnbd_sess_dev **res)
{
	int i, cnt = 0;

	for (i = 0; devs[i]; i++)
		if (match_device(devs[i], name))
			res[cnt++] = devs[i];

	res[cnt] = NULL;

	return cnt;
}

/*
 * Find all rnbd devices by device name, device path or mapping path
 */
static int find_devs_all(const char *name, enum rnbdmode rnbdmode,
			 struct rnbd_sess_dev **ds_imp,
			 int *ds_imp_cnt, struct rnbd_sess_dev **ds_exp,
			 int *ds_exp_cnt)
{
	int cnt_imp = 0, cnt_exp = 0;

	if (rnbdmode & RNBD_CLIENT)
		cnt_imp = find_devices(name, sds_clt, ds_imp);
	if (rnbdmode & RNBD_SERVER)
		cnt_exp = find_devices(name, sds_srv, ds_exp);

	*ds_imp_cnt = cnt_imp;
	*ds_exp_cnt = cnt_exp;

	return cnt_imp + cnt_exp;
}

static int show_device(struct rnbd_sess_dev **clt, struct rnbd_sess_dev **srv,
		       struct rnbd_ctx *ctx)
{
	struct table_fld flds[CLM_MAX_CNT];
	struct rnbd_sess_dev **ds;
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

static bool match_sess(struct rnbd_sess *s, const char *name)
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

static struct rnbd_sess *find_session(const char *name,
				       struct rnbd_sess **ss)
{
	int i;

	for (i = 0; ss[i]; i++)
		if (!strcmp(name, ss[i]->sessname))
			return ss[i];

	return NULL;
}

static int find_sessions_match(const char *name, struct rnbd_sess **ss,
			       struct rnbd_sess **res)
{
	int i, cnt = 0;

	for (i = 0; ss[i]; i++)
		if (match_sess(ss[i], name))
			res[cnt++] = ss[i];

	res[cnt] = NULL;

	return cnt;
}

static int find_sess_all(const char *name, enum rnbdmode rnbdmode,
			 struct rnbd_sess **ss_clt,
			 int *ss_clt_cnt, struct rnbd_sess **ss_srv,
			 int *ss_srv_cnt)
{
	int cnt_srv = 0, cnt_clt = 0;

	if (rnbdmode & RNBD_CLIENT)
		cnt_clt = find_sessions_match(name, sess_clt, ss_clt);
	if (rnbdmode & RNBD_SERVER)
		cnt_srv = find_sessions_match(name, sess_srv, ss_srv);

	*ss_clt_cnt = cnt_clt;
	*ss_srv_cnt = cnt_srv;

	return cnt_clt + cnt_srv;
}

static int parse_path1(const char *arg,
		       struct path *path)
{
	char *src, *dst;
	const char *d; int d_pos;

	d = strchr(arg, '@');
	if (d) {
		d_pos = d - arg;
		src = strndup(arg, d_pos+1);
		src[d_pos] = 0;
		dst = strdup(d + 1);
	} else {
		src = NULL;
		dst = strdup(arg);
	}

	if (src && !is_path_addr(src))
		goto free_error;

	if (!is_path_addr(dst))
		goto free_error;

	path->src = src;
	path->dst = dst;

	return 1;

free_error:
	if (src)
		free(src);
	if (dst)
		free(dst);

	return 0;
}

static bool match_path(struct rnbd_path *p, const char *name)
{
	struct path other = {NULL, NULL};
	int port;
	char *at;

	if (!strcmp(p->pathname, name) ||
	    !strcmp(name, p->src_addr) ||
	    !strcmp(name, p->dst_addr))
		return true;

	if (parse_path1(name, &other)) {
		if ((!other.src
		     || match_path_addr(p->src_addr, other.src))
		    && match_path_addr(p->dst_addr, other.dst))
			return true;
		if (!other.src
		     && match_path_addr(p->src_addr, other.dst))
			return true;
	}

	if ((sscanf(name, "%d\n", &port) == 1 &&
	     p->hca_port == port) ||
	    !strcmp(name, p->hca_name))
		return true;

	at = strrchr(name, ':');
	if (!at)
		return false;

	if (strncmp(p->sess->sessname, name,
		    strlen(p->sess->sessname)) &&
	    strncmp(p->hca_name, name,
		    strlen(p->hca_name)))
		return false;

	if ((sscanf(at + 1, "%d\n", &port) == 1 &&
	     p->hca_port == port) ||
	    !strcmp(at + 1, p->dst_addr) ||
	    !strcmp(at + 1, p->src_addr) ||
	    !strcmp(at + 1, p->hca_name))
		return true;

	return false;
}

static int find_paths(const char *name, struct rnbd_path **pp,
		      struct rnbd_path **res)
{
	int i, cnt = 0;

	for (i = 0; pp[i]; i++)
		if (match_path(pp[i], name))
			res[cnt++] = pp[i];

	res[cnt] = NULL;

	return cnt;
}

static int find_paths_all(const char *name, enum rnbdmode rnbdmode,
			  struct rnbd_path **pp_clt,
			  int *pp_clt_cnt, struct rnbd_path **pp_srv,
			  int *pp_srv_cnt)
{
	int cnt_clt = 0, cnt_srv = 0;

	if (rnbdmode & RNBD_CLIENT)
		cnt_clt = find_paths(name, paths_clt, pp_clt);
	if (rnbdmode & RNBD_SERVER)
		cnt_srv = find_paths(name, paths_srv, pp_srv);

	*pp_clt_cnt = cnt_clt;
	*pp_srv_cnt = cnt_srv;

	return cnt_clt + cnt_srv;
}

static int show_path(struct rnbd_path **pp_clt, struct rnbd_path **pp_srv,
		     struct rnbd_ctx *ctx)
{
	struct table_fld flds[CLM_MAX_CNT];
	struct table_column **cs;
	struct rnbd_path **pp;

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

static int show_session(struct rnbd_sess **ss_clt, struct rnbd_sess **ss_srv,
			struct rnbd_ctx *ctx)
{
	struct table_fld flds[CLM_MAX_CNT];
	struct table_column **cs, **ps;
	struct rnbd_sess **ss;

	if (ss_clt && ss_clt[0]) {
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
		if (ss[0]->side == RNBD_CLIENT)
			printf(" %s(%s)%s",
			       CLR(ctx->trm, CBLD, ss[0]->mp_short));
		printf("\n");
		list_paths_term(ss[0]->paths, ss[0]->path_cnt, ps, 1, ctx);

		break;
	}

	return 0;
}

static int show_all(const char *name, struct rnbd_ctx *ctx)
{
	struct rnbd_sess_dev **ds_clt, **ds_srv;
	struct rnbd_path **pp_clt, **pp_srv;
	struct rnbd_sess **ss_clt, **ss_srv;
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
		ERR(ctx->trm, "Failed to alloc memory\n");
		ret = -ENOMEM;
		goto out;
	}
	c_pp = find_paths_all(name, ctx->rnbdmode, pp_clt, &c_pp_clt, pp_srv,
			      &c_pp_srv);
	c_ss = find_sess_all(name, ctx->rnbdmode, ss_clt,
			     &c_ss_clt, ss_srv, &c_ss_srv);
	c_ds = find_devs_all(name, ctx->rnbdmode, ds_clt,
			     &c_ds_clt, ds_srv, &c_ds_srv);
	if (c_pp + c_ss + c_ds > 1) {
		ERR(ctx->trm, "Multiple entries match '%s'\n", name);
		if (c_pp) {
			printf("Paths:\n");
			list_paths(pp_clt, c_pp_clt,
				   pp_srv, c_pp_srv, false, ctx);
		}
		if (c_ss) {
			printf("Sessions:\n");
			list_sessions(ss_clt, c_ss_clt, ss_srv,
				      c_ss_srv, false, ctx);
		}
		if (c_ds) {
			printf("Devices:\n");
			list_devices(ds_clt, c_ds_clt, ds_srv,
				     c_ds_srv, false, ctx);
		}
		ret = -EINVAL;
		goto out;
	}

	if (c_ds)
		ret = show_device(ds_clt, ds_srv, ctx);
	else if (c_ss)
		ret = show_session(ss_clt, ss_srv, ctx);
	else if (c_pp)
		ret = show_path(pp_clt, pp_srv, ctx);
	else {
		ERR(ctx->trm, "There is no entry matching '%s'\n", name);
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

static int show_devices(const char *name, struct rnbd_ctx *ctx)
{
	struct rnbd_sess_dev **ds_clt, **ds_srv;
	int c_ds_clt, c_ds_srv, c_ds = 0, ret;

	ds_clt = calloc(sds_clt_cnt, sizeof(*ds_clt));
	ds_srv = calloc(sds_srv_cnt, sizeof(*ds_srv));

	if ((sds_clt_cnt && !ds_clt) ||
	    (sds_srv_cnt && !ds_srv)) {
		ERR(ctx->trm, "Failed to alloc memory\n");
		ret = -ENOMEM;
		goto out;
	}
	c_ds = find_devs_all(name, ctx->rnbdmode, ds_clt,
			     &c_ds_clt, ds_srv, &c_ds_srv);
	if (c_ds > 1) {
		ERR(ctx->trm, "Multiple devices match '%s'\n", name);

		printf("Devices:\n");
		list_devices(ds_clt, c_ds_clt, ds_srv, c_ds_srv, false, ctx);

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

#if 0
static int show_both_sessions(const char *name, struct rnbd_ctx *ctx)
{
	struct rnbd_sess **ss_clt, **ss_srv;
	int c_ss_clt, c_ss_srv, c_ss = 0, ret;

	ss_clt = calloc(sess_clt_cnt, sizeof(*ss_clt));
	ss_srv = calloc(sess_srv_cnt, sizeof(*ss_srv));

	if ((sess_clt_cnt && !ss_clt) ||
	    (sess_srv_cnt && !ss_srv)) {
		ERR(ctx->trm, "Failed to alloc memory\n");
		ret = -ENOMEM;
		goto out;
	}
	c_ss = find_sess_all(name, ctx->rnbdmode, ss_clt,
			     &c_ss_clt, ss_srv, &c_ss_srv);

	if (c_ss > 1) {
		ERR(ctx->trm, "Multiple sessions match '%s'\n", name);

		printf("Sessions:\n");
		list_sessions(ss_clt, c_ss_clt, ss_srv, c_ss_srv, false, ctx);

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
#endif

static int show_client_sessions(const char *name, struct rnbd_ctx *ctx)
{
	struct rnbd_sess **ss_clt;
	int c_ss_clt = 0, ret;

	ss_clt = calloc(sess_clt_cnt, sizeof(*ss_clt));

	if (sess_clt_cnt && !ss_clt) {
		ERR(ctx->trm, "Failed to alloc memory\n");
		ret = -ENOMEM;
		goto out;
	}
	ss_clt[0] = find_session(name, sess_clt);
	if (ss_clt[0])
		c_ss_clt = 1;
	else
		c_ss_clt = find_sessions_match(name, sess_clt, ss_clt);

	if (c_ss_clt > 1) {
		ERR(ctx->trm, "Multiple sessions match '%s'\n", name);

		printf("Sessions:\n");
		list_sessions(ss_clt, c_ss_clt, NULL, 0, false, ctx);

		ret = -EINVAL;
		goto out;
	}

	if (c_ss_clt) {
		ret = show_session(ss_clt, NULL, ctx);
	} else {
		ERR(ctx->trm, "There is no session matching '%s'\n", name);
		ret = -ENOENT;
	}
out:
	free(ss_clt);
	return ret;
}

static int show_server_sessions(const char *name, struct rnbd_ctx *ctx)
{
	struct rnbd_sess **ss_srv;
	int c_ss_srv = 0, ret;

	ss_srv = calloc(sess_srv_cnt, sizeof(*ss_srv));

	if (sess_srv_cnt && !ss_srv) {
		ERR(ctx->trm, "Failed to alloc memory\n");
		ret = -ENOMEM;
		goto out;
	}
	ss_srv[0] = find_session(name, sess_srv);
	if (ss_srv[0])
		c_ss_srv = 1;
	else
		c_ss_srv = find_sessions_match(name, sess_srv, ss_srv);

	if (c_ss_srv > 1) {
		ERR(ctx->trm, "Multiple sessions match '%s'\n", name);

		printf("Sessions:\n");
		list_sessions(NULL, 0, ss_srv, c_ss_srv, false, ctx);

		ret = -EINVAL;
		goto out;
	}

	if (c_ss_srv) {
		ret = show_session(NULL, ss_srv, ctx);
	} else {
		ERR(ctx->trm, "There is no session matching '%s'\n", name);
		ret = -ENOENT;
	}
out:
	free(ss_srv);
	return ret;
}

static int show_paths(const char *name, struct rnbd_ctx *ctx)
{
	struct rnbd_path **pp_clt, **pp_srv;
	int c_pp_clt, c_pp_srv, c_pp = 0, ret;

	pp_clt = calloc(paths_clt_cnt, sizeof(*pp_clt));
	pp_srv = calloc(paths_srv_cnt, sizeof(*pp_srv));

	if ((paths_clt_cnt && !pp_clt) ||
	    (paths_srv_cnt && !pp_srv)) {
		ERR(ctx->trm, "Failed to alloc memory\n");
		ret = -ENOMEM;
		goto out;
	}
	c_pp = find_paths_all(name, ctx->rnbdmode, pp_clt,
			      &c_pp_clt, pp_srv, &c_pp_srv);

	if (c_pp > 1) {
		ERR(ctx->trm, "Multiple paths match '%s'\n", name);

		printf("Paths:\n");
		list_paths(pp_clt, c_pp_clt, pp_srv, c_pp_srv, false, ctx);

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

static int parse_name_help(int argc, const char *argv[], const char *what,
			   const struct param *cmd, struct rnbd_ctx *ctx)
{
	const char *word;

	if (argc <= 0) {
		word = strchr(what, ' ');
		if (word)
			word++; /* skip space */
		else
			word = what;
		cmd_print_usage_short(cmd, what, ctx);
		ERR(ctx->trm, "Please specify the %s argument\n", word);
		return -EINVAL;
	}
	if (!strcmp(*argv, "help")) {
		parse_help(argc, argv, NULL, ctx);

		cmd->help(what, cmd, ctx);
		return -EAGAIN;
	}
	ctx->name = *argv;

	return 0;
}

static void help_show(const char *program_name,
		      const struct param *cmd,
		      const struct rnbd_ctx *ctx)
{
	cmd_print_usage_descr(cmd, "", ctx);

	if (!help_print_fields(ctx)) {

		printf("\nArguments:\n");
		print_opt("<name>",
			  "Name of an rnbd device, session, or path.");

		printf("\nOptions:\n");
	}
	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(ctx, def_clms_devices_clt, def_clms_devices_srv,
			     all_clms_devices, ctx->rnbdmode);
		print_fields(ctx, def_clms_sessions_clt, def_clms_sessions_srv,
			     all_clms_sessions, ctx->rnbdmode);
		print_fields(ctx, def_clms_paths_clt, def_clms_paths_srv,
			     all_clms_paths, ctx->rnbdmode);
		if (help_print_fields(ctx))
			return;
	}

	if (!help_print_all(ctx))
		printf("%sProvide 'all' to print all available fields\n\n",
		       HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");

	print_opt("help", "Display help and exit. [fields|all]");
}

static void help_show_devices(const char *program_name,
			      const struct param *cmd,
			      const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "devices";

	cmd_print_usage_descr(cmd, program_name, ctx);

	if (!help_print_fields(ctx)) {

		printf("\nArguments:\n");
		print_opt("<device>",
			  "Name of a local or a remote block device.");
		print_opt("",
			  "I.e. rnbd0, /dev/rnbd0, d12aef94-4110-4321-9373-3be8494a557b.");

		printf("\nOptions:\n");
	}
	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(ctx, def_clms_devices_clt, def_clms_devices_srv,
			     all_clms_devices, ctx->rnbdmode);
		if (help_print_fields(ctx))
			return;
	}

	if (!help_print_all(ctx))
		printf("%sProvide 'all' to print all available fields\n\n",
		       HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");

	print_opt("help", "Display help and exit. [fields|all]");
}

static void help_show_sessions(const char *program_name,
			       const struct param *cmd,
			       const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "sessions";

	cmd_print_usage_descr(cmd, program_name, ctx);

	if (!help_print_fields(ctx)) {

		printf("\nArguments:\n");
		print_opt("<session>",
			  "Session name or remote hostname.");
		print_opt("",
			  "I.e. ps401a-1@st401b-2, st401b-2, <ip1>@<ip2>, etc.");

		printf("\nOptions:\n");
	}
	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(ctx, def_clms_sessions_clt, def_clms_sessions_srv,
			     all_clms_sessions, ctx->rnbdmode);
		if (help_print_fields(ctx))
			return;
	}

	if (!help_print_all(ctx))
		printf("%sProvide 'all' to print all available fields\n\n",
		       HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");

	print_opt("help", "Display help and exit. [fields|all]");
}

static void help_show_paths(const char *program_name,
			    const struct param *cmd,
			    const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "paths";

	cmd_print_usage_descr(cmd, program_name, ctx);

	if (!help_print_fields(ctx)) {

		printf("\nArguments:\n");
		print_opt("<path>",
			  "In order to display path information, path name or identifier");
		print_opt("", "has to be provided, i.e. st401b-2:1.");

		printf("\nOptions:\n");
	}

	help_fields();

	if (help_print_all(ctx) || help_print_fields(ctx)) {

		print_fields(ctx, def_clms_paths_clt, def_clms_paths_srv,
			     all_clms_paths, ctx->rnbdmode);

		if (help_print_fields(ctx))
			return;
	}

	if (!help_print_all(ctx))
		printf("%sProvide 'all' to print all available fields\n\n",
		       HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");

	print_opt("help", "Display help and exit. [fields|all]");
}

static void help_map(const char *program_name,
		     const struct param *cmd,
		     const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "device ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<device>", "Path to the device to be mapped on server side");
	print_opt("from <server>",
		  "Address, hostname or session name of the server");

	printf("\nOptions:\n");
	print_opt("<path>", "Path(s) to establish: [src_addr@]dst_addr");
	print_opt("", "Address is [ip:]<ipv4>, [ip:]<ipv6> or gid:<gid>");

	print_opt("{rw}",
		  "Access permission on server side: ro|rw|migration. Default: rw");
	print_param_descr("verbose");
	print_param_descr("help");
}

static int parse_path(const char *arg,
		      struct rnbd_ctx *ctx)
{
	if (!parse_path1(arg, ctx->paths+ctx->path_cnt))
		return -EINVAL;

	ctx->path_cnt++;

	return 0;
}

static struct rnbd_sess *find_single_session(const char *session_name,
					      struct rnbd_ctx *ctx,
					      struct rnbd_sess **sessions,
					      int sess_cnt, bool print_err)
{
	struct rnbd_sess **matching_sess, *res = NULL;
	int match_count;

	if (!sess_cnt) {
		ERR(ctx->trm,
		    "Session '%s' not found: no sessions open\n", session_name);
		return NULL;
	}

	matching_sess = calloc(sess_cnt, sizeof(*matching_sess));

	if (sess_cnt && !matching_sess) {
		ERR(ctx->trm, "Failed to alloc memory\n");
		return NULL;
	}
	match_count = find_sessions_match(session_name, sessions,
					  matching_sess);

	if (match_count == 1) {
		res = *matching_sess;
	} else if (print_err) {
		ERR(ctx->trm, "%s '%s'.\n",
		    (match_count > 1)  ?
			"Please specify the session uniquely. There are multiple sessions matching"
			: "No session found matching",
		    session_name);
	}

	free(matching_sess);
	return res;
}

static struct rnbd_path *find_single_path(const char *path_name,
					   struct rnbd_ctx *ctx,
					   struct rnbd_path **paths,
					   int path_cnt)
{
	struct rnbd_path **matching_paths, *res = NULL;
	int match_count;

	if (!path_cnt) {
		ERR(ctx->trm,
		    "Path '%s' not found: there exists no paths\n", path_name);
		return NULL;
	}

	matching_paths = calloc(path_cnt, sizeof(*matching_paths));

	if (path_cnt && !matching_paths) {
		ERR(ctx->trm, "Failed to alloc memory\n");
		return NULL;
	}
	match_count = find_paths(path_name, paths, matching_paths);

	if (match_count == 1) {
		res = *matching_paths;
	} else {
		ERR(ctx->trm, "%s '%s'.\n",
		    (match_count > 1)  ?
			"Please specify the path uniquely. There are multiple paths matching"
			: "No path found matching",
		    path_name);
	}

	free(matching_paths);
	return res;
}

static int client_devices_map(const char *from_name, const char *device_name,
			      struct rnbd_ctx *ctx)
{
	char cmd[4096], sessname[NAME_MAX];
	struct rnbd_sess *sess;
	struct rnbd_path *path;
	int i, cnt = 0, ret;
	bool existing_session_used = false;

	if (!ctx->path_cnt && !parse_path(ctx->from, ctx)) {
		/* user provided only a path to establish */

		path = find_single_path(from_name, ctx,
					paths_clt, paths_clt_cnt);
		if (path) {
			sess = path->sess;
			strcpy(sessname, from_name);
			INF(ctx->debug_set,
			    "map matched session %s for path name %s.\n",
			    sess->sessname, from_name);
			strcpy(sessname, sess->sessname);
		} else {
			ERR(ctx->trm,
			    "Client session for path '%s' not found. Please provide a session name to establish a new session.\n",
			    ctx->from);
			return -EINVAL;
		}
	} else {
		strcpy(sessname, from_name);
		sess = find_session(sessname, sess_clt);
		if (sess)
			existing_session_used = true;
	}
	if (!sess && !strchr(from_name, '@')) {

		/* if it looks like a host name, try to match a session */
		sess = find_single_session(from_name, ctx, sess_clt,
					   sds_clt_cnt, false);

		if (sess) {
			INF(ctx->debug_set,
			    "map matched session %s for name %s.\n",
			    sess->sessname, from_name);
			strcpy(sessname, sess->sessname);
		} else {

			INF(ctx->debug_set,
			    "map found no matching session for %s.\n",
			    from_name);

			/* if still not found, generate the session name */
			ret = sessname_from_host(from_name,
						 sessname, sizeof(sessname));
			if (ret) {
				ERR(ctx->trm,
				    "Failed to generate session name for %s: %s (%d)\n",
				    from_name, strerror(-ret), ret);
				return ret;
			}
			if (!ctx->path_cnt) {

				/* if no path provided by user, */
				/* try to resolve from_name as host name */
				ret = resolve_host(from_name, ctx->paths, ctx);
				if (ret < 0) {
					ERR(ctx->trm,
					    "Failed to resolve host name for %s: %s (%d)\n",
					    from_name, strerror(-ret), ret);
					return ret;
				} else if (ret == 0) {
					ERR(ctx->trm,
					    "Found no paths to host %s.\n",
					    from_name);
					return ret;
				} else {
					ctx->path_cnt = ret;
				}
			}
		}
	}
	if (!sess && !ctx->path_cnt) {
		ERR(ctx->trm,
		    "Client session '%s' not found. Please provide at least one path to establish a new one.\n",
		    from_name);
		return -EINVAL;
	}

	if (existing_session_used && ctx->path_cnt)
		INF(ctx->verbose_set,
		    "Session '%s' exists. Provided paths will be ignored by the driver. Please use addpath to add a path to an existsing sesion.\n",
		    from_name);

	cnt = snprintf(cmd, sizeof(cmd), "sessname=%s", sessname);
	cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt, " device_path=%s",
			device_name);

	for (i = 0; i < ctx->path_cnt; i++)
		if (ctx->paths[i].src)
			cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt,
					" path=%s@%s",
					ctx->paths[i].src, ctx->paths[i].dst);
		else
			cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt,
					" path=%s",
					ctx->paths[i].dst);

	if (sess)
		for (i = 0; i < sess->path_cnt; i++)
			cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt,
					" path=%s@%s", sess->paths[i]->src_addr,
					sess->paths[i]->dst_addr);

	if (ctx->access_mode_set)
		cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt, " access_mode=%s",
				ctx->access_mode);

	ret = printf_sysfs(PATH_IBNBD_CLT, "map_device", ctx, "%s", cmd);
	if (ret)
		ERR(ctx->trm, "Failed to map device: %s (%d)\n",
		    strerror(-ret), ret);
	else
		INF(ctx->verbose_set, "Successfully mapped '%s' from '%s'.\n",
		    device_name, from_name);

	return ret;
}

static struct rnbd_sess_dev *find_single_device(const char *name,
						 struct rnbd_ctx *ctx,
						 struct rnbd_sess_dev **devs,
						 int dev_cnt)
{
	struct rnbd_sess_dev *res = NULL, **matching_devs;
	int match_count;

	if (!dev_cnt) {
		ERR(ctx->trm,
		    "Device '%s' not found: no devices mapped\n", name);
		return NULL;
	}

	matching_devs = calloc(dev_cnt, sizeof(*matching_devs));
	if (!matching_devs) {
		ERR(ctx->trm, "Failed to allocate memory\n");
		return NULL;
	}

	match_count = find_devices(name, devs, matching_devs);
	if (match_count == 1) {

		res = matching_devs[0];
	} else {
		ERR(ctx->trm, "%s '%s'.\n",
		    (match_count > 1)  ?
			"Please specify an unique device. There are multiple devices matching"
			: "Did not found device",
		    name);
	}

	free(matching_devs);
	return res;
}

static int client_devices_resize(const char *device_name, uint64_t size_sect,
				 struct rnbd_ctx *ctx)
{
	const struct rnbd_sess_dev *ds;
	char tmp[PATH_MAX];
	int ret;

	ds = find_single_device(device_name, ctx, sds_clt, sds_clt_cnt);
	if (!ds)
		return -EINVAL;

	sprintf(tmp, "/sys/block/%s/ibnbd", ds->dev->devname);
	ret = printf_sysfs(tmp, "resize", ctx, "%d", size_sect);
	if (ret)
		ERR(ctx->trm, "Failed to resize %s: %s (%d)\n",
		    ds->dev->devname, strerror(-ret), ret);
	else
		INF(ctx->verbose_set,
		    "Device '%s' resized sucessfully to %lu sectors.\n",
		    ds->dev->devname, size_sect);

	return ret;
}

static void help_resize(const char *program_name,
			const struct param *cmd,
			const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "<device name or path or mapping path> ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<device>", "Name of the device to be resized");
	print_opt("<size>", "New size of the device in bytes");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");

	printf("\nOptions:\n");
	print_param_descr("verbose");
	print_param_descr("help");
}

static void help_unmap(const char *program_name,
		       const struct param *cmd,
		       const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "<device name or path or mapping path> ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<device>", "Name of the device to be unmapped");

	printf("\nOptions:\n");
	print_param_descr("force");
	print_param_descr("verbose");
	print_param_descr("help");
}

static int client_devices_unmap(const char *device_name, bool force,
				struct rnbd_ctx *ctx)
{
	const struct rnbd_sess_dev *ds;
	char tmp[PATH_MAX];
	int ret;

	ds = find_single_device(device_name, ctx, sds_clt, sds_clt_cnt);
	if (!ds)
		return -EINVAL;

	sprintf(tmp, "/sys/block/%s/ibnbd", ds->dev->devname);
	ret = printf_sysfs(tmp, "unmap_device", ctx, "%s",
			   force ? "force" : "normal");
	if (ret)
		ERR(ctx->trm, "Failed to %sunmap '%s': %s (%d)\n",
		    force ? "force-" : "",
		    ds->dev->devname, strerror(-ret), ret);
	else
		INF(ctx->verbose_set, "Device '%s' sucessfully unmapped.\n",
		    ds->dev->devname);

	return ret;
}

static int client_device_remap(const struct rnbd_dev *dev,
			       struct rnbd_ctx *ctx)
{
	char tmp[PATH_MAX];
	int ret;

	sprintf(tmp, "/sys/block/%s/ibnbd", dev->devname);
	ret = printf_sysfs(tmp, "remap_device", ctx, "1");
	if (ret == -EALREADY) {
		INF(ctx->verbose_set,
		    "Device '%s' does not need to be remapped.\n",
		    dev->devname);
		ret = 0;
	} else if (ret) {
		ERR(ctx->trm, "Failed to remap %s: %s (%d)\n",
		    dev->devname, strerror(-ret), ret);
	} else {
		INF(ctx->verbose_set,
		    "Device '%s' sucessfully remapped.\n",
		    dev->devname);
	}
	return ret;
}

static int client_devices_remap(const char *device_name, struct rnbd_ctx *ctx)
{
	const struct rnbd_sess_dev *ds;

	ds = find_single_device(device_name, ctx, sds_clt, sds_clt_cnt);
	if (!ds)
		return -EINVAL;

	return client_device_remap(ds->dev, ctx);
}

static int client_session_remap(const char *session_name,
				 struct rnbd_ctx *ctx)
{
	int tmp_err, err = 0;
	struct rnbd_sess_dev *const *sds_iter;
	const struct rnbd_sess *sess;

	if (!sds_clt_cnt) {
		ERR(ctx->trm,
		    "No devices mapped. Nothing to be done!\n");
		return -EINVAL;
	}

	sess = find_single_session(session_name, ctx, sess_clt,
				   sds_clt_cnt, true);
	if (!sess)
		return -EINVAL;

	for (sds_iter = sds_clt; *sds_iter; sds_iter++) {

		if ((*sds_iter)->sess == sess) {
			tmp_err = client_device_remap((*sds_iter)->dev, ctx);
			/*  intentional continue on error */
			if (!err && tmp_err)
				err = tmp_err;
		}
	}
	return err;
}

static int session_do_all_paths(enum rnbdmode mode,
				const char *session_name,
				int (*do_it)(const char *path_name,
					     struct rnbd_ctx *ctx),
				struct rnbd_ctx *ctx)
{
	int err = 0;
	struct rnbd_path *const *paths_iter;
	const struct rnbd_sess *sess;

	if (!(mode == RNBD_CLIENT ?
	      sess_clt_cnt
	      : sess_srv_cnt)) {
		ERR(ctx->trm,
		    "No sessions opened!\n");
		return -EINVAL;
	}

	if (mode == RNBD_CLIENT)
		sess = find_single_session(session_name, ctx,
					   sess_clt, sess_clt_cnt,
					   true);
	else
		sess = find_single_session(session_name, ctx,
					   sess_srv, sess_srv_cnt,
					   true);

	if (!sess)
		/*find_single_session has printed an error message*/
		return -EINVAL;

	for (paths_iter = (mode == RNBD_CLIENT ? paths_clt : paths_srv);
	     *paths_iter && !err; paths_iter++) {

		if ((*paths_iter)->sess == sess)
			err = do_it((*paths_iter)->pathname, ctx);
	}
	return err;
}

static void help_remap(const char *program_name,
		       const struct param *cmd,
		       const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "<device name> ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>", "Identifier of a device to be remapped.");

	printf("\nOptions:\n");
	print_param_descr("force");
	print_param_descr("verbose");
	print_param_descr("help");
}

static void help_remap_device_or_session(const char *program_name,
					 const struct param *cmd,
					 const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "<name> ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>", "Identifier of a device or session to be remapped.");
	print_opt("", "If identifier designates a session,");
	print_opt("", "all devices of this particular session will be remapped.");

	printf("\nOptions:\n");
	print_param_descr("force");
	print_param_descr("verbose");
	print_param_descr("help");
}

static void help_remap_session(const char *program_name,
			       const struct param *cmd,
			       const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "<session name> ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<session>",
		  "Identifier of a session to remap all devices on.");

	printf("\nOptions:\n");
	print_param_descr("force");
	print_param_descr("verbose");
	print_param_descr("help");
}

static void help_reconnect_session(const char *program_name,
				   const struct param *cmd,
				   const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "<session> ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<session>", "Name or identifier of a session.");

	printf("\nOptions:\n");
	print_param_descr("verbose");
	print_param_descr("help");
}

static void help_reconnect_path(const char *program_name,
				const struct param *cmd,
				const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "<path> ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>",
		  "Name or identifier of a path:");
	print_opt("", "[pathname], [sessname:port], etc.");

	printf("\nOptions:\n");
	print_param_descr("verbose");
	print_param_descr("help");
}

static void help_disconnect_session(const char *program_name,
				    const struct param *cmd,
				    const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "<session> ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<session>", "Name or identifier of a session.");

	printf("\nOptions:\n");
	print_param_descr("verbose");
	print_param_descr("help");
}

static void help_disconnect_path(const char *program_name,
				 const struct param *cmd,
				 const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "<path> ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<identifier>", "Name or identifier of of a path:");
	print_opt("", "[pathname], [sessname:port], etc.");

	printf("\nOptions:\n");
	print_param_descr("verbose");
	print_param_descr("help");
}

static int client_session_add(const char *session_name,
			      const struct path *path,
			      struct rnbd_ctx *ctx)
{
	char address_string[4096];
	char sysfs_path[4096];
	struct rnbd_sess *sess;
	int ret;

	sess = find_session(session_name, sess_clt);

	if (!sess) {
		ERR(ctx->trm,
		    "Session '%s' does not exists.\n", session_name);
		return -EINVAL;
	}

	snprintf(sysfs_path, sizeof(sysfs_path),
		 PATH_SESS_CLT "%s", sess->sessname);
	if (path->src)
		snprintf(address_string, sizeof(address_string),
			 "%s@%s", path->src, path->dst);
	else
		snprintf(address_string, sizeof(address_string),
			 "%s", path->dst);

	ret = printf_sysfs(sysfs_path, "add_path", ctx, "%s", address_string);
	if (ret)
		ERR(ctx->trm,
		    "Failed to add path '%s' to session '%s': %s (%d)\n",
		    sess->sessname, address_string, strerror(-ret), ret);
	else
		INF(ctx->verbose_set, "Successfully added path '%s' to '%s'.\n",
		    address_string, sess->sessname);
	return ret;
}

static void help_addpath(const char *program_name,
			 const struct param *cmd,
			 const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "<session> <path> ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<session>",
		  "Name of the session to add the new path to");
	print_opt("<path>",
		  "Path to be added: [src_addr@]dst_addr");
	print_opt("",
		  "Address is of the form ip:<ipv4>, ip:<ipv6> or gid:<gid>");

	printf("\nOptions:\n");
	print_param_descr("verbose");
	print_param_descr("help");
}

static void help_delpath(const char *program_name,
			 const struct param *cmd,
			 const struct rnbd_ctx *ctx)
{
	if (!program_name)
		program_name = "<path> ";

	cmd_print_usage_descr(cmd, program_name, ctx);

	printf("\nArguments:\n");
	print_opt("<path>",
		  "Name or any unique identifier of a path:");
	print_opt("", "[pathname], [sessname:port], etc.");

	printf("\nOptions:\n");
	print_param_descr("verbose");
	print_param_descr("help");
}

static int client_path_do(const char *path_name, const char *sysfs_entry,
			  const char *message_success,
			  const char *message_fail, struct rnbd_ctx *ctx)
{
	char sysfs_path[4096];
	struct rnbd_path *path;
	int ret;

	path = find_single_path(path_name, ctx, paths_clt, paths_clt_cnt);

	if (!path)
		return -EINVAL;

	snprintf(sysfs_path, sizeof(sysfs_path),
		 PATH_SESS_CLT "%s/paths/%s",
		 path->sess->sessname, path->pathname);

	ret = printf_sysfs(sysfs_path, sysfs_entry, ctx, "1");
	if (ret)
		ERR(ctx->trm, message_fail, path->pathname,
		    path->sess->sessname, strerror(-ret), ret);
	else
		INF(ctx->verbose_set, message_success,
		    path->pathname, path->sess->sessname);
	return ret;
}

static int client_path_delete(const char *path_name,
				 struct rnbd_ctx *ctx)
{
	return client_path_do(path_name, "remove_path",
				 "Successfully removed path '%s' from '%s'.\n",
				 "Failed to remove path '%s' from session '%s': %s (%d)\n",
				 ctx);
}

static int client_path_reconnect(const char *path_name,
				    struct rnbd_ctx *ctx)
{
	return client_path_do(path_name, "reconnect",
			      "Successfully reconnected path '%s' of session '%s'.\n",
			      "Failed to reconnect path '%s' from session '%s': %s (%d)\n",
			      ctx);
}

static int client_path_disconnect(const char *path_name,
				  struct rnbd_ctx *ctx)
{
	return client_path_do(path_name, "disconnect",
			      "Successfully disconnected path '%s' from session '%s'.\n",
			      "Failed to disconnect path '%s' of session '%s': %s (%d)\n",
			      ctx);
}

static int client_path_readd(const char *path_name,
			     struct rnbd_ctx *ctx)
{
	char sysfs_path[4096];
	struct rnbd_path *path;
	int ret;

	path = find_single_path(path_name, ctx, paths_clt, paths_clt_cnt);

	if (!path)
		return -EINVAL;

	snprintf(sysfs_path, sizeof(sysfs_path),
		 PATH_SESS_CLT "%s/paths/%s",
		 path->sess->sessname, path->pathname);

	ret = printf_sysfs(sysfs_path, "remove_path", ctx, "1");
	if (ret) {
		ERR(ctx->trm,
		    "Failed to remove path '%s' from session '%s': %s (%d)\n",
		    path->pathname,
		    path->sess->sessname, strerror(-ret), ret);
		return ret;
	}
	INF(ctx->verbose_set, "Successfully removed path '%s' from '%s'.\n",
	    path->pathname, path->sess->sessname);

	snprintf(sysfs_path, sizeof(sysfs_path),
		 PATH_SESS_CLT "%s", path->sess->sessname);

	ret = printf_sysfs(sysfs_path, "add_path", ctx, "%s", path->pathname);
	if (ret)
		ERR(ctx->trm,
		    "Failed to readd path '%s' to session '%s': %s (%d)\n",
		    path->sess->sessname, path->pathname, strerror(-ret), ret);
	else
		INF(ctx->verbose_set,
		    "Successfully readded path '%s' to '%s'.\n",
		    path->pathname, path->sess->sessname);
	return ret;
}

static int server_path_disconnect(const char *path_name,
				  struct rnbd_ctx *ctx)
{
	char sysfs_path[4096];
	struct rnbd_path *path;
	int ret;

	path = find_single_path(path_name, ctx, paths_srv, paths_srv_cnt);

	if (!path)
		return -EINVAL;

	snprintf(sysfs_path, sizeof(sysfs_path),
		 PATH_SESS_SRV "%s/paths/%s",
		 path->sess->sessname, path->pathname);

	ret = printf_sysfs(sysfs_path, "disconnect", ctx, "1");
	if (ret)
		ERR(ctx->trm,
		    "Failed to disconnect path '%s' of session '%s': %s (%d)\n",
		    path->pathname,
		    path->sess->sessname, strerror(-ret), ret);
	else
		INF(ctx->verbose_set,
		    "Successfully disconnected path '%s' from session '%s'.\n",
		    path->pathname, path->sess->sessname);
	return ret;
}

static struct param _cmd_dump_all =
	{TOK_DUMP, "dump",
		"Dump information about all",
		"",
		"Dump information about all rnbd objects.",
		NULL, NULL, help_dump_all};
static struct param _cmd_list_devices =
	{TOK_LIST, "list",
		"List information on all",
		"s",
		"List information on devices.",
		NULL, NULL, help_list_devices};
static struct param _cmd_list_sessions =
	{TOK_LIST, "list",
		"List information on all",
		"s",
		"List information on sessions.",
		NULL, NULL, help_list_sessions};
static struct param _cmd_list_paths =
	{TOK_LIST, "list",
		"List information on all",
		"s",
		"List information on paths.",
		NULL, NULL, help_list_paths};
static struct param _cmd_show =
	{TOK_SHOW, "show",
		"Show information about the object that is designated by <name>",
		"",
		"Show information about an rnbd device, session, or path.",
	 "<name>",
		NULL, help_show};
static struct param _cmd_show_devices =
	{TOK_SHOW, "show",
		"Show information about a",
		"",
		"Show information about an rnbd block device.",
		"<device>",
		NULL, help_show_devices};
static struct param _cmd_show_sessions =
	{TOK_SHOW, "show",
		"Show information about a",
		"",
		"Show information about an rnbd session.",
		"<session>",
		NULL, help_show_sessions};
static struct param _cmd_show_paths =
	{TOK_SHOW, "show",
		"Show information about a",
		"",
		"Show information about an rnbd transport path.",
		"<path>",
		NULL, help_show_paths};
static struct param _cmd_map =
	{TOK_MAP, "map",
		"Map a",
		" from a given server",
		"Map a device from a given server",
		"<device> from <server>",
		 NULL, help_map};
static struct param _cmd_resize =
	{TOK_RESIZE, "resize",
		"Resize a mapped",
		"",
		"Change size of a mapped device",
		"<device> <size>",
		 NULL, help_resize};
static struct param _cmd_unmap =
	{TOK_UNMAP, "unmap",
		"Unmap an imported",
		"",
		"Umap a given imported device",
		"<device>",
		NULL, help_unmap};
static struct param _cmd_remap =
	{TOK_REMAP, "remap",
		"Remap a",
		"",
		"Unmap and map again an imported device",
		"<device>",
		 NULL, help_remap};
static struct param _cmd_remap_device_or_session =
	{TOK_REMAP, "remap",
		"Remap a",
		"",
		"Unmap and map again a devices or all devices of a given session",
		"<device|session>",
		 NULL, help_remap_device_or_session};
static struct param _cmd_remap_session =
	{TOK_REMAP, "remap",
		"Remap all devicess on a",
		"",
		"Unmap and map again all devices of a given session",
		"<session>",
		 NULL, help_remap_session};
static struct param _cmd_disconnect_session =
	{TOK_DISCONNECT, "disconnect",
		"Disconnect a",
		"",
		"Disconnect all paths on a given session",
		"<session>",
		NULL, help_disconnect_session};
static struct param _cmd_dis_session =
	{TOK_DISCONNECT, "dis",
		"Disconnect a",
		"",
		"Disconnect all paths on a given session",
		"<session>",
		NULL, help_disconnect_session};
static struct param _cmd_disconnect_path =
	{TOK_DISCONNECT, "disconnect",
		"Disconnect a",
		"",
		"Disconnect a path of a given session",
		"<path>",
		NULL, help_disconnect_path};
static struct param _cmd_dis_path =
	{TOK_DISCONNECT, "dis",
		"Disconnect a",
		"",
		"Disconnect a path of a given session",
		"<path>",
		NULL, help_disconnect_path};
static struct param _cmd_reconnect_session =
	{TOK_RECONNECT, "reconnect",
		"Reconnect a",
		"",
		"Disconnect and connect again a whole session",
		"<session>",
		 NULL, help_reconnect_session};
static struct param _cmd_rec_session =
	{TOK_RECONNECT, "rec",
		"Reconnect a",
		"",
		"Disconnect and connect again a whole session",
		"<session>",
		 NULL, help_reconnect_session};
static struct param _cmd_reconnect_path =
	{TOK_RECONNECT, "reconnect",
		"Reconnect a",
		"",
		"Disconnect and connect again a single path of a session",
		"<path>",
		 NULL, help_reconnect_path};
static struct param _cmd_rec_path =
	{TOK_RECONNECT, "rec",
		"Reconnect a",
		"",
		"Disconnect and connect again a single path of a session",
		"<path>",
		 NULL, help_reconnect_path};
static struct param _cmd_add =
	{TOK_ADD, "add",
		"Add a",
		" to an existing session",
		"Add a new path to an existing session",
		"<session> <path>",
		 NULL, help_addpath};
static struct param _cmd_delete =
	{TOK_DELETE, "delete",
		"Delete a",
		"",
		"Delete a given path from the corresponding session",
		"<path>",
		 NULL, help_delpath};
static struct param _cmd_del =
	{TOK_DELETE, "del",
		"Delete a",
		"",
		"Delete a given path from the corresponding session",
		"<path>",
		 NULL, help_delpath};
static struct param _cmd_readd =
	{TOK_READD, "readd",
		"Readd a",
		"",
		"Delete and add again a given path to the corresponding session",
		"<path>",
		 NULL, help_delpath};
static struct param _cmd_help =
	{TOK_HELP, "help",
		"Display help on",
		"s",
		"Display help message and exit.",
		NULL, NULL, help_object};
static struct param _cmd_null =
		{ 0 };

static struct param *params_flags[] = {
	&_params_minus_minus_help,
	&_params_minus_h,
	&_params_minus_minus_verbose,
	&_params_minus_v,
	&_params_minus_minus_debug,
	&_params_minus_d,
	&_params_minus_minus_simulate,
	&_params_minus_s,
	&_params_minus_c,
	&_params_minus_minus_complete,
	&_params_null
};

static struct param *params_flags_help[] = {
	&_params_minus_minus_help,
	&_params_minus_minus_verbose,
	&_params_minus_minus_debug,
	&_params_minus_minus_simulate,
	&_params_null
};

static struct param *params_default[] = {
	&_params_help,
	&_params_verbose,
	&_params_minus_v,
	&_params_null
};

static struct param *params_mode[] = {
	&_params_client,
	&_params_clt,
	&_params_cli,
	&_params_server,
	&_params_serv,
	&_params_srv,
	&_params_both,
	&_params_help,
	&_params_version,
	&_params_null
};

static struct param *params_mode_help[] = {
	&_params_client,
	&_params_server,
	&_params_both,
	&_params_help,
	&_params_version,
	&_params_null
};

static struct param *params_both[] = {
	&_cmd_list_devices,
	&_cmd_dump_all,
	&_cmd_show,
	&_cmd_map,
	&_cmd_resize,
	&_cmd_unmap,
	&_cmd_remap,
	&_params_help,
	&_params_null
};

static struct param *params_both_help[] = {
	&_cmd_dump_all,
	&_params_help,
	&_params_null
};

static struct param *params_object_type_client[] = {
	&_params_devices,
	&_params_device,
	&_params_devs,
	&_params_dev,
	&_params_sessions,
	&_params_session,
	&_params_sess,
	&_params_paths,
	&_params_path,
	&_cmd_dump_all,
	&_cmd_list_devices,
	&_cmd_show,
	&_cmd_map,
	&_cmd_resize,
	&_cmd_unmap,
	&_cmd_remap_device_or_session,
	&_params_help,
	&_params_null
};

static struct param *params_object_type_server[] = {
	&_params_devices,
	&_params_device,
	&_params_devs,
	&_params_dev,
	&_params_sessions,
	&_params_session,
	&_params_sess,
	&_params_paths,
	&_params_path,
	&_cmd_dump_all,
	&_cmd_list_devices,
	&_cmd_show,
	&_params_help,
	&_params_null
};

static struct param *params_object_type_help_client[] = {
	&_params_devices_client,
	&_params_sessions,
	&_params_paths,
	&_params_help,
	&_params_null
};

static struct param *params_object_type_help_server[] = {
	&_params_devices,
	&_params_sessions,
	&_params_paths,
	&_params_help,
	&_params_null
};

static struct param *params_list_parameters[] = {
	&_params_xml,
	&_params_cvs,
	&_params_json,
	&_params_term,
	&_params_byte,
	&_params_kib,
	&_params_mib,
	&_params_gib,
	&_params_tib,
	&_params_pib,
	&_params_eib,
	&_params_notree,
	&_params_noheaders,
	&_params_nototals,
	&_params_noterm,
	&_params_all,
	&_params_verbose,
	&_params_help,
	&_params_null
};

static struct param *params_fmt_parameters[] = {
	&_params_xml,
	&_params_cvs,
	&_params_json,
	&_params_term,
	&_params_null
};

static struct param *params_map_from_parameters[] = {
	&_params_from,
	&_params_help,
	&_params_verbose,
	&_params_minus_v,
	&_params_null
};

static struct param *params_map_parameters[] = {
	&_params_from,
	&_params_ro,
	&_params_rw,
	&_params_migration,
	&_params_help,
	&_params_verbose,
	&_params_minus_v,
	&_params_null
};

static struct param *params_map_parameters_help[] = {
	&_params_from,
	&_params_path_param,
	&_params_ro,
	&_params_rw,
	&_params_migration,
	&_params_help,
	&_params_verbose,
	&_params_minus_v,
	&_params_null
};

static struct param *params_unmap_parameters[] = {
	&_params_help,
	&_params_force,
	&_params_verbose,
	&_params_minus_v,
	&_params_null
};

static struct param *params_add_path_parameters[] = {
	&_params_help,
	&_params_verbose,
	&_params_minus_v,
	&_params_null
};

static struct param *params_add_path_help[] = {
	&_params_help,
	&_params_path_param,
	&_params_verbose,
	&_params_null
};

static struct param *cmds_client_sessions[] = {
	&_cmd_list_sessions,
	&_cmd_show_sessions,
	&_cmd_reconnect_session,
	&_cmd_rec_session,
	&_cmd_remap_session,
	&_cmd_help,
	&_cmd_null
};

static struct param *cmds_client_sessions_help[] = {
	&_cmd_list_sessions,
	&_cmd_show_sessions,
	&_cmd_reconnect_session,
	&_cmd_remap_session,
	&_cmd_help,
	&_cmd_null
};

static struct param *cmds_client_devices[] = {
	&_cmd_list_devices,
	&_cmd_show_devices,
	&_cmd_map,
	&_cmd_resize,
	&_cmd_unmap,
	&_cmd_remap,
	&_cmd_help,
	&_cmd_null
};

static struct param *cmds_client_paths[] = {
	&_cmd_list_paths,
	&_cmd_show_paths,
	&_cmd_disconnect_path,
	&_cmd_dis_path,
	&_cmd_reconnect_path,
	&_cmd_rec_path,
	&_cmd_add,
	&_cmd_delete,
	&_cmd_del,
	&_cmd_readd,
	&_cmd_help,
	&_cmd_null
};

static struct param *cmds_client_paths_help[] = {
	&_cmd_list_paths,
	&_cmd_show_paths,
	&_cmd_disconnect_path,
	&_cmd_reconnect_path,
	&_cmd_add,
	&_cmd_delete,
	&_cmd_readd,
	&_cmd_help,
	&_cmd_null
};

static struct param *cmds_server_sessions[] = {
	&_cmd_list_sessions,
	&_cmd_show_sessions,
	&_cmd_disconnect_session,
	&_cmd_dis_session,
	&_cmd_help,
	&_cmd_null
};

static struct param *cmds_server_sessions_help[] = {
	&_cmd_list_sessions,
	&_cmd_show_sessions,
	&_cmd_disconnect_session,
	&_cmd_help,
	&_cmd_null
};

static struct param *cmds_server_devices[] = {
	&_cmd_list_devices,
	&_cmd_show_devices,
	&_cmd_help,
	&_cmd_null
};

static struct param *cmds_server_paths[] = {
	&_cmd_list_paths,
	&_cmd_show_paths,
	&_cmd_disconnect_path,
	&_cmd_dis_path,
	&_cmd_help,
	&_cmd_null
};

static struct param *cmds_server_paths_help[] = {
	&_cmd_list_paths,
	&_cmd_show_paths,
	&_cmd_disconnect_path,
	&_cmd_help,
	&_cmd_null
};

static int levenstein_compare(int d1, int d2, const char *s1, const char *s2)
{
	return d1 != d2 ? d1 - d2 : strcmp(s1, s2);
}

static int param_compare(const void *p1, const void *p2)
{
	const struct param *const*c1 = p1;
	const struct param *const*c2 = p2;

	return levenstein_compare((*c1)->dist, (*c2)->dist,
				  (*c1)->param_str, (*c2)->param_str);
}

static void handle_unknown_param(const char *param, struct param *params[])
{
	struct param **cs;
	size_t len = 0, cnt = 0, i;

	printf("Unknown: %s\n", param);

	for (cs = params; (*cs)->param_str; cs++) {
		(*cs)->dist = levenshtein((*cs)->param_str, param, 1, 2, 1, 0)
				+ 1;
		if (strlen((*cs)->param_str) < 2)
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

		qsort(params, len, sizeof(*params), param_compare);
		printf("Did you mean:\n");
	}
	if (cnt > 3)
		cnt = 3;

	for (i = 0; i < cnt; i++)
		printf("\t%s\n", params[i]->param_str);
}

static int parse_precision(const char *str,
			   struct rnbd_ctx *ctx)
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

static int parse_clt_devices_clms(const char *arg, struct rnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_devices_clt,
				    ctx->clms_devices_clt, CLM_MAX_CNT);
}

static int parse_srv_devices_clms(const char *arg, struct rnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_devices_srv,
				    ctx->clms_devices_srv, CLM_MAX_CNT);
}

static int parse_clt_sessions_clms(const char *arg, struct rnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_sessions_clt,
				    ctx->clms_sessions_clt, CLM_MAX_CNT);
}

static int parse_srv_sessions_clms(const char *arg, struct rnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_sessions_srv,
				    ctx->clms_sessions_srv, CLM_MAX_CNT);
}

static int parse_clt_paths_clms(const char *arg, struct rnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_paths_clt,
				    ctx->clms_paths_clt, CLM_MAX_CNT);
}

static int parse_srv_paths_clms(const char *arg, struct rnbd_ctx *ctx)
{
	return table_extend_columns(arg, comma, all_clms_paths_srv,
				    ctx->clms_paths_srv, CLM_MAX_CNT);
}

static int parse_clt_clms(const char *arg, struct rnbd_ctx *ctx)
{
	int tmp_err, err;

	err = table_extend_columns(arg, comma, all_clms_devices_clt,
				   ctx->clms_devices_clt, CLM_MAX_CNT);
	tmp_err = table_extend_columns(arg, comma, all_clms_sessions_clt,
				       ctx->clms_sessions_clt, CLM_MAX_CNT);
	if (!tmp_err && err)
		err = tmp_err;

	tmp_err = table_extend_columns(arg, comma, all_clms_paths_clt,
				       ctx->clms_paths_clt, CLM_MAX_CNT);
	if (!tmp_err && err)
		err = tmp_err;

	/* if any of the parse commands succeed return success */
	return err;
}

static int parse_srv_clms(const char *arg, struct rnbd_ctx *ctx)
{
	int tmp_err, err;

	err = table_extend_columns(arg, comma, all_clms_devices_srv,
				   ctx->clms_devices_srv, CLM_MAX_CNT);
	tmp_err = table_extend_columns(arg, comma, all_clms_sessions_srv,
				       ctx->clms_sessions_srv, CLM_MAX_CNT);
	if (!tmp_err && err)
		err = tmp_err;

	tmp_err = table_extend_columns(arg, comma, all_clms_paths_srv,
				       ctx->clms_paths_srv, CLM_MAX_CNT);
	if (!tmp_err && err)
		err = tmp_err;

	/* if any of the parse commands succeed return success */
	return err;
}

static int parse_both_devices_clms(const char *arg, struct rnbd_ctx *ctx)
{
	int tmp_err, err;

	err = table_extend_columns(arg, comma, all_clms_devices_clt,
				    ctx->clms_devices_clt, CLM_MAX_CNT);
	tmp_err = table_extend_columns(arg, comma, all_clms_devices_srv,
				    ctx->clms_devices_srv, CLM_MAX_CNT);
	if (!tmp_err && err)
		err = tmp_err;

	/* if any of the parse commands succeed return success */
	return err;
}

static int parse_both_clms(const char *arg, struct rnbd_ctx *ctx)
{
	int tmp_err, err;

	err =  parse_clt_clms(arg, ctx);
	tmp_err = parse_srv_clms(arg, ctx);

	if (!tmp_err && err)
		err = tmp_err;

	/* if any of the parse commands succeed return success */
	return err;
}

static int parse_sign(char s,
		      struct rnbd_ctx *ctx)
{
	if (s == '+')
		ctx->sign = 1;
	else if (s == '-')
		ctx->sign = -1;
	else
		ctx->sign = 0;

	return ctx->sign;
}

static int parse_size(const char *str,
		      struct rnbd_ctx *ctx)
{
	uint64_t size;

	if (parse_sign(*str, ctx))
		str++;

	if (str_to_size(str, &size))
		return -EINVAL;

	ctx->size_sect = size >> 9;
	ctx->size_set = 1;

	return 0;
}

static void init_rnbd_ctx(struct rnbd_ctx *ctx)
{
	memset(ctx, 0, sizeof(struct rnbd_ctx));

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

	ctx->trm = (isatty(STDOUT_FILENO) == 1);

}

static void deinit_rnbd_ctx(struct rnbd_ctx *ctx)
{
	int i;

	for (i = 0; i < ctx->path_cnt; i++) {
		if (ctx->paths[i].src)
			free((char *)ctx->paths[i].src);
		if (ctx->paths[i].dst)
			free((char *)ctx->paths[i].dst);
	}
}

static void rnbd_ctx_default(struct rnbd_ctx *ctx)
{
	if (!ctx->lstmode_set)
		ctx->lstmode = LST_DEVICES;

	if (!ctx->fmt_set)
		ctx->fmt = FMT_TERM;

	if (!ctx->prec_set)
		ctx->prec = 3;

	if (!ctx->rnbdmode_set) {
		if (sess_clt[0])
			ctx->rnbdmode |= RNBD_CLIENT;
		if (sess_srv[0])
			ctx->rnbdmode |= RNBD_SERVER;
	}
}

static void help_mode(const char *mode, struct param *const params[],
		      const struct rnbd_ctx *ctx)
{
	char buff[PATH_MAX];

	if (ctx->pname_with_mode) {

		help_param(ctx->pname, params, ctx);
	} else {

		snprintf(buff, sizeof(buff), "%s %s", ctx->pname, mode);
		help_param(buff, params, ctx);
	}
}

static void help_start(const struct rnbd_ctx *ctx)
{
	if (help_print_flags(ctx) || help_print_all(ctx)) {
		help_param(ctx->pname, params_flags_help, ctx);
		if (help_print_all(ctx))
			printf("\n\n");
	}
	if (!help_print_flags(ctx))
		help_param(ctx->pname, params_mode_help, ctx);

	if (help_print_all(ctx)) {

		printf("\n\n");
		help_mode("client",
			  params_object_type_help_client, ctx);
		printf("\n\n");
		help_mode("server",
			  params_object_type_help_server, ctx);
		printf("\n\n");
		help_mode("both", params_both_help, ctx);
	}
}

static void print_version(const struct rnbd_ctx *ctx)
{
	printf("%s version %s%s%s\n", ctx->pname,
	       CLR(ctx->trm, CBLD, PACKAGE_VERSION));
}

/**
 * Parse all the possible parameters to list or show commands.
 * The results are collected in the rnbd_ctx struct
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
int parse_list_parameters(int argc, const char *argv[], struct rnbd_ctx *ctx,
		int (*parse_clms)(const char *arg, struct rnbd_ctx *ctx),
		const struct param *cmd, const char *program_name)
{
	int err = 0; int start_argc = argc;
	const struct param *param;

	while (argc && err >= 0) {
		/* parse the list flags */
		param = find_param(*argv, params_list_parameters);
		if (param) {
			err = param->parse(argc, argv, param, ctx);
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
	if (ctx->help_set) {
		cmd->help(program_name, cmd, ctx);
		err = -EAGAIN;
	} else if (err < 0) {
		handle_unknown_param(*argv, params_list_parameters);
	}
	return err < 0 ? err : start_argc - argc;
}

/**
 * Parse parameters for a command as described by cmd.
 */
int parse_cmd_parameters(int argc, const char *argv[],
			 struct param *const params[], struct rnbd_ctx *ctx,
			 const struct param *cmd, const char *program_name)
{
	int err = 0; int start_argc = argc;
	const struct param *param;

	while (argc && err >= 0) {
		param = find_param(*argv, params);
		if (param)
			err = param->parse(argc, argv, param, ctx);

		if (!param || err <= 0)
			break;

		argc -= err; argv += err;
	}
	if (ctx->help_set && cmd) {
		cmd->help(program_name, cmd, ctx);
		err = -EAGAIN;
	}
	return err < 0 ? err : start_argc - argc;
}

int parse_all_parameters(int argc, const char *argv[],
			 struct param *const params[], struct rnbd_ctx *ctx,
			 const struct param *cmd, const char *program_name)
{
	int err = 0;
	const struct param *param;

	while (argc && err >= 0) {
		param = find_param(*argv, params);
		if (param)
			err = param->parse(argc, argv, param, ctx);
		else
			err = 1; /* skip unknown parameter */

		argc -= err; argv += err;
	}
	return err;
}

/**
 * Parse parameters for the map command as described by cmd.
 */
int parse_map_parameters(int argc, const char *argv[], int *accepted,
			 struct param *const params[], struct rnbd_ctx *ctx,
			 const struct param *cmd, const char *program_name)
{
	int err = 0; int start_argc = argc;
	const struct param *param;

	while (argc && err >= 0) {
		param = find_param(*argv, params);
		if (param) {
			err = param->parse(argc, argv, param, ctx);
		} else {
			err = parse_path(*argv, ctx);
			if (err == 0)
				err = 1;
		}
		if (err <= 0)
			break;

		argc -= err; argv += err;
	}
	if (ctx->help_set) {
		cmd->help(program_name, cmd, ctx);
		err = -EAGAIN;
	}
	*accepted = start_argc - argc;
	return err;
}

int cmd_dump_all(int argc, const char *argv[], const struct param *cmd,
		 const char *help_context, struct rnbd_ctx *ctx)

{
	int err, tmp_err;

	err = parse_all_parameters(argc, argv, params_fmt_parameters,
				   ctx, cmd, "");
	if (err < 0)
		return err;

	/* default format for dump is all except when output to terminal */
	if (ctx->fmt == FMT_TERM) {
		ctx->notree_set = true;
		ctx->nototals_set = true;
	} else	{
		(void) parse_all(0, NULL, NULL, ctx);
	}
	/* parse from beginning again! */
	err = parse_list_parameters(argc, argv, ctx,
				    parse_both_clms,
				    cmd, "");
	if (err < 0)
		return err;

	ctx->rnbdmode = RNBD_BOTH;

	err = list_devices(sds_clt, sds_clt_cnt - 1, sds_srv,
			   sds_srv_cnt - 1, true, ctx);

	if ((sds_clt_cnt - 1 + sds_srv_cnt - 1)
	    && (sess_clt_cnt - 1 + sess_srv_cnt - 1))
		printf("\n");

	tmp_err = list_sessions(sess_clt, sess_clt_cnt - 1, sess_srv,
				sess_srv_cnt - 1, true, ctx);

	if (!err && tmp_err)
		err = tmp_err;

	if ((sds_clt_cnt - 1 + sds_srv_cnt - 1
	     + sess_clt_cnt - 1 + sess_srv_cnt - 1)
	    && (paths_clt_cnt - 1 + paths_srv_cnt - 1))
		printf("\n");

	tmp_err = list_paths(paths_clt, paths_clt_cnt - 1, paths_srv,
			     paths_srv_cnt - 1, true, ctx);

	if (!err && tmp_err)
		err = tmp_err;

	return err;
}

int cmd_map(int argc, const char *argv[], const struct param *cmd,
	    const char *help_context, struct rnbd_ctx *ctx)
{
	int accepted = 0, err = 0;

	err = parse_name_help(argc--, argv++,
			      help_context, cmd, ctx);
	if (err < 0)
		return err;

	err = parse_map_parameters(argc, argv, &accepted,
				   params_map_from_parameters,
				   ctx, cmd, help_context);
	if (accepted == 0) {

		cmd_print_usage_short(cmd, help_context, ctx);
		ERR(ctx->trm,
		    "Please specify the destination to map from\n");

		return -EINVAL;
	}
	argc -= accepted; argv += accepted;

	if (argc > 0 || (err < 0 && err != -EAGAIN)) {

		handle_unknown_param(*argv,
				    params_map_parameters);
		if (err < 0) {
			printf("\n");
			print_param(ctx->pname,
				   params_map_parameters_help,
				   ctx);
		}
		return -EINVAL;
	}
	return err = client_devices_map(ctx->from, ctx->name, ctx);
}

int cmd_resize(int argc, const char *argv[], const struct param *cmd,
	       const char *help_context, struct rnbd_ctx *ctx)
{
	int err = parse_name_help(argc--, argv++,
				  help_context, cmd, ctx);
	if (err < 0)
		return err;

	if (argc > 0)
		err = parse_size(*argv, ctx);
	else
		err = -EINVAL;
	if (err < 0) {
		cmd_print_usage_short(cmd, help_context, ctx);
		ERR(ctx->trm,
		    "Please provide the size of device to be configured\n");
		return err;
	}
	argc--; argv++;

	if (argc > 0) {
		err = parse_apply_unit(*argv, ctx);
		if (err < 0) {
			if (!strcmp(*argv, "help")) {
				parse_help(argc, argv, NULL, ctx);

				cmd->help(*argv, cmd, ctx);
				return -EAGAIN;
			} else {
				ERR(ctx->trm,
				    "Please provide a valid unit for size of device to be configured\n");
				return err;
			}
		}
	}
	argc--; argv++;

	if (argc > 0) {

		handle_unknown_param(*argv, params_default);
		return -EINVAL;
	}
	return client_devices_resize(ctx->name, ctx->size_sect, ctx);
}

int cmd_unmap(int argc, const char *argv[], const struct param *cmd,
	      const char *help_context, struct rnbd_ctx *ctx)
{
	int err = parse_name_help(argc--, argv++,
				  help_context, cmd, ctx);
	if (err < 0)
		return err;

	err = parse_cmd_parameters(argc, argv,
				   params_unmap_parameters,
				   ctx, cmd, help_context);
	if (err < 0)
		return err;

	argc -= err; argv += err;

	if (argc > 0) {

		handle_unknown_param(*argv,
				     params_unmap_parameters);
		return -EINVAL;
	}
	return client_devices_unmap(ctx->name, ctx->force_set, ctx);
}

int cmd_remap(int argc, const char *argv[], const struct param *cmd,
	      bool allowSession, const char *help_context, struct rnbd_ctx *ctx)
{
	int err = parse_name_help(argc--, argv++,
				  help_context, cmd, ctx);
	if (err < 0)
		return err;

	err = parse_cmd_parameters(argc, argv, params_default,
				   ctx, cmd, help_context);
	if (err < 0)
		return err;

	argc -= err; argv += err;

	if (argc > 0) {

		handle_unknown_param(*argv, params_default);
		return -EINVAL;
	}
	if (allowSession
	    && find_single_session(ctx->name, ctx, sess_clt,
				   sds_clt_cnt, false))
		    return client_session_remap(ctx->name, ctx);

	return client_devices_remap(ctx->name, ctx);
}

int cmd_client_sessions(int argc, const char *argv[], struct rnbd_ctx *ctx)
{
	const char *_help_context = ctx->pname_with_mode
		? "session" : "client session";

	int err = 0;
	const struct param *cmd;

	cmd = find_param(*argv, cmds_client_sessions);
	if (!cmd) {
		print_usage(_help_context, cmds_client_sessions_help, ctx);
		if (ctx->complete_set)
			err = -EAGAIN;
		else
			err = -EINVAL;

		if (argc)
			handle_unknown_param(*argv, cmds_client_sessions);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
						    parse_clt_sessions_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = list_sessions(sess_clt, sess_clt_cnt - 1,
					    NULL, 0, false, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						    parse_clt_sessions_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = show_client_sessions(ctx->name, ctx);
			break;
		case TOK_RECONNECT:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_cmd_parameters(argc, argv, params_default,
						   ctx, cmd, _help_context);
			if (err < 0)
				break;

			argc -= err; argv += err;

			if (argc > 0) {

				handle_unknown_param(*argv, params_default);
				err = -EINVAL;
				break;
			}
			/* We want the session to change it's state to */
			/* disconnected. So disconnect all paths first.*/
			err = session_do_all_paths(RNBD_CLIENT, ctx->name,
						   client_path_disconnect,
						   ctx);
			/* If the session does not exist at all we will   */
			/* get -EINVAL. In all other error cases we try   */
			/* to reconnect the path to reconnect the session.*/
			if (err != -EINVAL)
				err = session_do_all_paths(RNBD_CLIENT,
							ctx->name,
							client_path_reconnect,
							ctx);
			break;
		case TOK_REMAP:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_cmd_parameters(argc, argv,
						   params_default,
						   ctx, cmd, _help_context);
			if (err < 0)
				break;

			argc -= err; argv += err;

			if (argc > 0) {

				handle_unknown_param(*argv, params_default);
				err = -EINVAL;
				break;
			}
			err = client_session_remap(ctx->name, ctx);
			break;
		case TOK_HELP:
			parse_help(argc, argv, NULL, ctx);
			print_help(_help_context,
				   cmd, cmds_client_sessions_help, ctx);
			break;
		default:
			print_usage(_help_context,
				    cmds_client_sessions_help, ctx);
			handle_unknown_param(cmd->param_str,
					    cmds_client_sessions);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_client_devices(int argc, const char *argv[], struct rnbd_ctx *ctx)
{
	const char *_help_context = ctx->pname_with_mode
		? "device" : "client device";

	int err = 0;
	const struct param *cmd;

	cmd = find_param(*argv, cmds_client_devices);
	if (!cmd) {
		print_usage(_help_context, cmds_client_devices, ctx);
		if (ctx->complete_set)
			err = -EAGAIN;
		else
			err = -EINVAL;

		if (argc)
			handle_unknown_param(*argv, cmds_client_devices);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
						    parse_clt_devices_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = list_devices(sds_clt, sds_clt_cnt - 1,
					   NULL, 0, false, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						    parse_clt_devices_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = show_devices(ctx->name, ctx);
			break;
		case TOK_MAP:
			err = cmd_map(argc, argv, cmd, _help_context, ctx);
			break;
		case TOK_RESIZE:
			err = cmd_resize(argc, argv, cmd, _help_context, ctx);
			break;
		case TOK_UNMAP:
			err = cmd_unmap(argc, argv, cmd, _help_context, ctx);
			break;
		case TOK_REMAP:
			err = cmd_remap(argc, argv, cmd, false, _help_context, ctx);
			break;
		case TOK_HELP:
			parse_help(argc, argv, NULL, ctx);
			print_help(_help_context, cmd,
				   cmds_client_devices, ctx);
			break;
		default:
			print_usage(_help_context, cmds_client_devices, ctx);
			handle_unknown_param(cmd->param_str,
					     cmds_client_devices);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_client_paths(int argc, const char *argv[], struct rnbd_ctx *ctx)
{
	const char *_help_context = ctx->pname_with_mode
		? "path" : "client path";

	int accepted = 0, err = 0;
	const struct param *cmd;

	cmd = find_param(*argv, cmds_client_paths);
	if (!cmd) {
		print_usage(_help_context, cmds_client_paths_help, ctx);
		if (ctx->complete_set)
			err = -EAGAIN;
		else
			err = -EINVAL;

		if (argc)
			handle_unknown_param(*argv, cmds_client_paths);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
						    parse_clt_paths_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = list_paths(paths_clt, paths_clt_cnt - 1,
					 NULL, 0, false, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						    parse_clt_paths_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = show_paths(ctx->name, ctx);
			break;
		case TOK_DISCONNECT:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_cmd_parameters(argc, argv, params_default,
						   ctx, cmd, _help_context);
			if (err < 0)
				break;

			argc -= err; argv += err;

			if (argc > 0) {

				handle_unknown_param(*argv, params_default);
				err = -EINVAL;
				break;
			}
			err = client_path_disconnect(ctx->name, ctx);
			break;
		case TOK_RECONNECT:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_cmd_parameters(argc, argv, params_default,
						   ctx, cmd, _help_context);
			if (err < 0)
				break;

			argc -= err; argv += err;

			if (argc > 0) {

				handle_unknown_param(*argv, params_default);
				err = -EINVAL;
				break;
			}
			err = client_path_reconnect(ctx->name, ctx);
			break;
		case TOK_ADD:
			err = parse_name_help(argc--, argv++,
					      "session", cmd, ctx);
			if (err < 0)
				break;

			err = parse_map_parameters(argc, argv, &accepted,
						   params_add_path_parameters,
						   ctx, cmd, _help_context);
			if (accepted == 0) {

				cmd_print_usage_short(cmd, _help_context, ctx);
				ERR(ctx->trm,
				    "Please specify the path to add to session %s\n",
				    ctx->name);
				print_opt(_params_path_param.param_str,
					  _params_path_param.descr);
				err = -EINVAL;
				break;
			}
			argc -= accepted; argv += accepted;

			if (argc > 0 || (err < 0 && err != -EAGAIN)) {

				handle_unknown_param(*argv,
						     params_add_path_help);
				if (err < 0) {
					printf("\n");
					print_param(ctx->pname,
						   params_add_path_help,
						   ctx);
				}
				err = -EINVAL;
				break;
			}
			if (ctx->path_cnt <= 0) {
				cmd_print_usage_short(cmd, _help_context, ctx);
				ERR(ctx->trm,
				    "No valid path provided. Please provide a path to add to session '%s'.\n",
				    ctx->name);
			}
			if (ctx->path_cnt > 1) {
				cmd_print_usage_short(cmd, _help_context, ctx);
				ERR(ctx->trm,
				    "You provided %d paths. Please provide exactly one path to add to session '%s'.\n",
				    ctx->path_cnt, ctx->name);
			}
			err = client_session_add(ctx->name, ctx->paths, ctx);
			break;
		case TOK_DELETE:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_cmd_parameters(argc, argv, params_default,
						   ctx, cmd, _help_context);
			if (err < 0)
				break;

			argc -= err; argv += err;

			if (argc > 0) {

				handle_unknown_param(*argv, params_default);
				err = -EINVAL;
				break;
			}
			err = client_path_delete(ctx->name, ctx);
			break;
		case TOK_READD:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_cmd_parameters(argc, argv, params_default,
						   ctx, cmd, _help_context);
			if (err < 0)
				break;

			argc -= err; argv += err;

			if (argc > 0) {

				handle_unknown_param(*argv, params_default);
				err = -EINVAL;
				break;
			}
			err = client_path_readd(ctx->name, ctx);
			break;
		case TOK_HELP:
			parse_help(argc, argv, NULL, ctx);
			print_help(_help_context, cmd,
				   cmds_client_paths_help, ctx);
			break;
		default:
			print_usage(_help_context, cmds_client_paths_help, ctx);
			handle_unknown_param(cmd->param_str,
					    cmds_client_paths);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_server_sessions(int argc, const char *argv[], struct rnbd_ctx *ctx)
{
	const char *_help_context = ctx->pname_with_mode
		? "session" : "server session";

	int err = 0;
	const struct param *cmd;

	cmd = find_param(*argv, cmds_server_sessions);
	if (!cmd) {
		print_usage(_help_context, cmds_server_sessions_help, ctx);
		if (ctx->complete_set)
			err = -EAGAIN;
		else
			err = -EINVAL;

		if (argc)
			handle_unknown_param(*argv, cmds_server_sessions);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
						    parse_srv_sessions_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = list_sessions(NULL, 0, sess_srv,
					    sess_srv_cnt - 1, false, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						    parse_srv_sessions_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = show_server_sessions(ctx->name, ctx);
			break;
		case TOK_DISCONNECT:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_cmd_parameters(argc, argv, params_default,
						   ctx, cmd, _help_context);
			if (err < 0)
				break;

			argc -= err; argv += err;

			if (argc > 0) {

				handle_unknown_param(*argv, params_default);
				err = -EINVAL;
				break;
			}
			err = session_do_all_paths(RNBD_SERVER, ctx->name,
						   server_path_disconnect,
						   ctx);
			break;
		case TOK_HELP:
			parse_help(argc, argv, NULL, ctx);
			print_help(_help_context, cmd,
				   cmds_server_sessions_help, ctx);
			break;
		default:
			print_usage(_help_context,
				    cmds_server_sessions_help, ctx);
			handle_unknown_param(cmd->param_str,
					    cmds_server_sessions);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_server_devices(int argc, const char *argv[], struct rnbd_ctx *ctx)
{
	const char *_help_context = ctx->pname_with_mode
		? "device" : "server device";

	int err = 0;
	const struct param *cmd;

	cmd = find_param(*argv, cmds_server_devices);
	if (!cmd) {
		print_usage(_help_context, cmds_server_devices, ctx);
		if (ctx->complete_set)
			err = -EAGAIN;
		else
			err = -EINVAL;

		if (argc)
			handle_unknown_param(*argv, cmds_server_devices);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
						    parse_srv_devices_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = list_devices(NULL, 0, sds_srv,
					   sds_srv_cnt - 1, false, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						    parse_srv_devices_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = show_devices(ctx->name, ctx);
			break;
		case TOK_HELP:
			parse_help(argc, argv, NULL, ctx);
			print_help(_help_context, cmd,
				   cmds_server_devices, ctx);
			break;
		default:
			print_usage(_help_context, cmds_server_devices, ctx);
			handle_unknown_param(cmd->param_str,
					    cmds_client_sessions);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_server_paths(int argc, const char *argv[], struct rnbd_ctx *ctx)
{
	const char *_help_context = ctx->pname_with_mode
		? "path" : "server path";

	int err = 0;
	const struct param *cmd;

	cmd = find_param(*argv, cmds_server_paths);
	if (!cmd) {
		print_usage(_help_context, cmds_server_paths_help, ctx);
		if (ctx->complete_set)
			err = -EAGAIN;
		else
			err = -EINVAL;

		if (argc)
			handle_unknown_param(*argv, cmds_server_paths);
	}
	if (err >= 0) {

		argc--; argv++;

		switch (cmd->tok) {
		case TOK_LIST:
			err = parse_list_parameters(argc, argv, ctx,
						    parse_srv_paths_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = list_paths(NULL, 0, paths_srv,
					 paths_srv_cnt - 1, false, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						    parse_srv_devices_clms,
						    cmd, _help_context);
			if (err < 0)
				break;

			err = show_paths(ctx->name, ctx);
			break;
		case TOK_DISCONNECT:
			err = parse_name_help(argc--, argv++,
					      _help_context, cmd, ctx);
			if (err < 0)
				break;

			err = parse_cmd_parameters(argc, argv, params_default,
						   ctx, cmd, _help_context);
			if (err < 0)
				break;

			argc -= err; argv += err;

			if (argc > 0) {

				handle_unknown_param(*argv, params_default);
				err = -EINVAL;
				break;
			}
			err = server_path_disconnect(ctx->name, ctx);
			break;
		case TOK_HELP:
			parse_help(argc, argv, NULL, ctx);
			print_help(_help_context, cmd,
				   cmds_server_paths_help, ctx);
			break;
		default:
			print_usage(_help_context, cmds_server_paths_help, ctx);
			handle_unknown_param(cmd->param_str,
					    cmds_client_sessions);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_client(int argc, const char *argv[], struct rnbd_ctx *ctx)
{
	const char *_help_context = "client";
	int err = 0;
	const struct param *param;

	if (argc < 1) {
		usage_param("rnbd client",
			    params_object_type_help_client, ctx);
		if (ctx->complete_set) {
			err = -EAGAIN;
		} else {
			ERR(ctx->trm, "no object specified\n");
			err = -EINVAL;
		}
	}
	if (err >= 0) {
		param = find_param(*argv, params_object_type_client);
		if (!param) {
			usage_param("rnbd client",
				   params_object_type_help_client, ctx);
			handle_unknown_param(*argv, params_object_type_client);
			err = -EINVAL;
		} else if (param->parse) {
			(void) param->parse(argc, argv, param, ctx);
		}
	}

	argc--; argv++;

	if (err >= 0) {
		switch (param->tok) {
		case TOK_DEVICES:
			err = cmd_client_devices(argc, argv, ctx);
			break;
		case TOK_SESSIONS:
			err = cmd_client_sessions(argc, argv, ctx);
			break;
		case TOK_PATHS:
			err = cmd_client_paths(argc, argv, ctx);
			break;
		case TOK_DUMP:
			err = cmd_dump_all(argc, argv, param, "", ctx);
			break;
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
						    parse_clt_devices_clms,
						    param, _help_context);
			if (err < 0)
				break;

			err = list_devices(sds_clt, sds_clt_cnt - 1,
					   NULL, 0, false, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      "name", param, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						    parse_clt_clms,
						    param, _help_context);
			if (err < 0)
				break;

			err = show_all(ctx->name, ctx);
			break;
		case TOK_MAP:
			err = cmd_map(argc, argv, param, _help_context, ctx);
			break;
		case TOK_RESIZE:
			err = cmd_resize(argc, argv, param, _help_context, ctx);
			break;
		case TOK_UNMAP:
			err = cmd_unmap(argc, argv, param, _help_context, ctx);
			break;
		case TOK_REMAP:
			err = cmd_remap(argc, argv, param, true, _help_context, ctx);
			break;
		case TOK_HELP:
			if (ctx->pname_with_mode
			    && (help_print_flags(ctx) || help_print_all(ctx))) {
				help_param(ctx->pname, params_flags_help, ctx);
				if (help_print_all(ctx))
					printf("\n\n");
			}
			if (!ctx->pname_with_mode || !help_print_flags(ctx))
				help_mode("client",
					  params_object_type_help_client, ctx);
			break;
		default:
			usage_param("rnbd client",
				   params_object_type_help_client, ctx);
			handle_unknown_param(*argv, params_object_type_client);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_server(int argc, const char *argv[], struct rnbd_ctx *ctx)
{
	const char *_help_context = "server";
	int err = 0;
	const struct param *param;

	if (argc < 1) {
		usage_param("rnbd server",
			    params_object_type_help_server, ctx);
		if (ctx->complete_set) {
			err = -EAGAIN;
		} else {
			ERR(ctx->trm, "no object specified\n");
			err = -EINVAL;
		}
	}
	if (err >= 0) {
		param = find_param(*argv, params_object_type_server);
		if (!param) {
			usage_param("rnbd server",
				   params_object_type_help_server, ctx);
			handle_unknown_param(*argv, params_object_type_server);
			err = -EINVAL;
		} else if (param->parse) {
			(void) param->parse(argc, argv, param, ctx);
		}
	}

	argc--; argv++;

	if (err >= 0) {
		switch (param->tok) {
		case TOK_DEVICES:
			err = cmd_server_devices(argc, argv, ctx);
			break;
		case TOK_SESSIONS:
			err = cmd_server_sessions(argc, argv, ctx);
			break;
		case TOK_PATHS:
			err = cmd_server_paths(argc, argv, ctx);
			break;
		case TOK_DUMP:
			err = cmd_dump_all(argc, argv, param, "", ctx);
			break;
		case TOK_LIST:

			err = parse_list_parameters(argc, argv, ctx,
						    parse_srv_devices_clms,
						    param, _help_context);
			if (err < 0)
				break;

			err = list_devices(NULL, 0, sds_srv,
					   sds_srv_cnt - 1, false, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      "name", param, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						    parse_srv_clms,
						    param, _help_context);
			if (err < 0)
				break;

			err = show_all(ctx->name, ctx);
			break;
		case TOK_HELP:
			if (ctx->pname_with_mode
			    && (help_print_flags(ctx) || help_print_all(ctx))) {
				help_param(ctx->pname, params_flags_help, ctx);
				if (help_print_all(ctx))
					printf("\n\n");
			}
			if (!ctx->pname_with_mode || !help_print_flags(ctx))
				help_mode("server",
					  params_object_type_help_server,
					  ctx);
			break;
		default:
			usage_param("rnbd server",
				   params_object_type_help_server, ctx);
			handle_unknown_param(*argv, params_object_type_server);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_both(int argc, const char *argv[], struct rnbd_ctx *ctx)
{
	int err = 0;
	const struct param *param;

	if (argc < 1) {
		usage_param("rnbd both", params_both_help, ctx);
		if (ctx->complete_set)
			err = -EAGAIN;
		else
			err = -EINVAL;
	}
	if (err >= 0) {
		param = find_param(*argv, params_both);
		if (!param) {
			handle_unknown_param(*argv, params_both);
			usage_param(ctx->pname, params_both_help, ctx);
			err = -EINVAL;
		} else if (param->parse) {
			(void) param->parse(argc, argv, param, ctx);
		}
	}

	argc--; argv++;

	if (err >= 0) {
		switch (param->tok) {
		case TOK_DUMP:
			err = cmd_dump_all(argc, argv, param, "", ctx);
			break;
		case TOK_LIST:
			err = parse_list_parameters(argc, argv, ctx,
						    parse_both_devices_clms,
						    param, "");
			if (err < 0)
				break;

			err = list_devices(sds_clt, sds_clt_cnt - 1,
					   sds_srv, sds_srv_cnt - 1,
					   false, ctx);
			break;
		case TOK_SHOW:
			err = parse_name_help(argc--, argv++,
					      "name", param, ctx);
			if (err < 0)
				break;

			err = parse_list_parameters(argc, argv, ctx,
						    parse_both_clms,
						    param, "");
			if (err < 0)
				break;

			err = show_all(ctx->name, ctx);
			break;
		case TOK_MAP:
			err = cmd_map(argc, argv, param, "", ctx);
			break;
		case TOK_RESIZE:
			err = cmd_resize(argc, argv, param, "", ctx);
			break;
		case TOK_UNMAP:
			err = cmd_unmap(argc, argv, param, "", ctx);
			break;
		case TOK_REMAP:
			err = cmd_remap(argc, argv, param, false, "", ctx);
			break;
		case TOK_HELP:
			help_param(ctx->pname, params_both_help, ctx);

			if (help_print_all(ctx)) {

				help_mode("client",
					  params_object_type_help_client, ctx);
				help_mode("server",
					  params_object_type_help_server, ctx);
				help_mode("both", params_both_help, ctx);
			}
			break;
		default:
			handle_unknown_param(*argv, params_both);
			usage_param(ctx->pname, params_both_help, ctx);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int cmd_start(int argc, const char *argv[], struct rnbd_ctx *ctx)
{
	int err = 0;
	const struct param *param;

	if (argc < 1) {
		usage_param(ctx->pname, params_mode_help, ctx);
		ERR(ctx->trm, "mode not specified\n");
		err = -EINVAL;
	}
	if (err >= 0) {
		param = find_param(*argv, params_mode);
		if (!param) {
			ctx->rnbdmode = mode_for_host();

			INF(ctx->debug_set,
			    "RNBD mode deduced from sysfs: '%s'.\n",
			    mode_to_string(ctx->rnbdmode));

			switch (ctx->rnbdmode) {
			case RNBD_CLIENT:
				return cmd_client(argc, argv, ctx);
			case RNBD_SERVER:
				return cmd_server(argc, argv, ctx);
			case RNBD_BOTH:
				return cmd_both(argc, argv, ctx);
			default:
				ERR(ctx->trm,
				    "RNBD mode not specified and could not be deduced.\n");
				print_usage(NULL, params_mode_help, ctx);

				handle_unknown_param(*argv, params_mode);
				usage_param(ctx->pname, params_mode_help, ctx);
				err = -EINVAL;
			}
		} else if (param->parse) {
			(void) param->parse(argc, argv, param, ctx);
		}
	}

	if (err >= 0) {
		switch (param->tok) {
		case TOK_CLIENT:
			err = cmd_client(--argc, ++argv, ctx);
			break;
		case TOK_SERVER:
			err = cmd_server(--argc, ++argv, ctx);
			break;
		case TOK_BOTH:
			err = cmd_both(--argc, ++argv, ctx);
			break;
		case TOK_HELP:
			help_start(ctx);
			break;
		case TOK_VERSION:
			print_version(ctx);
			break;
		default:
			handle_unknown_param(*argv, params_mode);
			usage_param(ctx->pname, params_mode_help, ctx);
			err = -EINVAL;
			break;
		}
	}
	return err;
}

int main(int argc, const char *argv[])
{
	int ret = 0;

	struct rnbd_ctx ctx;

	init_rnbd_ctx(&ctx);

	parse_argv0(argv[0], &ctx);

	ret = rnbd_sysfs_alloc_all(&sds_clt, &sds_srv,
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

	ret = rnbd_sysfs_read_all(sds_clt, sds_srv, sess_clt, sess_srv,
				   paths_clt, paths_srv);
	if (ret) {
		ERR(ctx.trm, "Failed to read sysfs entries: %d\n", ret);
		goto free;
	}

	rnbd_ctx_default(&ctx);

	ret = parse_cmd_parameters(--argc, ++argv, params_flags,
				   &ctx, NULL, NULL);
	if (ret < 0)
		goto free;

	argc -= ret; argv += ret;

	if (argc && *argv[0] == '-') {
		handle_unknown_param(*argv, params_flags);
		help_param(ctx.pname, params_flags_help, &ctx);
		ret = -EINVAL;
		goto free;
	} else if (ctx.help_set) {
		help_start(&ctx);
		ret = -EINVAL;
		goto free;
	}
	ret = cmd_start(argc, argv, &ctx);

free:
	rnbd_sysfs_free_all(sds_clt, sds_srv, sess_clt, sess_srv,
			     paths_clt, paths_srv);
out:
	deinit_rnbd_ctx(&ctx);

	if (ret == -EAGAIN)
		/* help message was printed */
		ret = 0;

	return ret;
}
