#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>	/* for isatty() */
#include <assert.h>
#include <stdbool.h>

#include "levenshtein.h"
#include "table.h"
#include "misc.h"
#include "list.h"

#include "ibnbd-sysfs.h"
#include "ibnbd-clms.h"

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

static struct ibnbd_sess_dev **sds_clt;
static struct ibnbd_sess_dev **sds_srv;
static struct ibnbd_sess **sess_clt;
static struct ibnbd_sess **sess_srv;
static struct ibnbd_path **paths_clt;
static struct ibnbd_path **paths_srv;

/*
 * True if STDOUT is a terminal
 */
static int trm;

struct path {
	const char *src;
	const char *dst;
};

struct args {
	const char *pname;
	const char *name;

	uint64_t size_sect;
	bool size_set;
	short sign;

	enum fmt_type fmt;
	bool fmt_set;

	char io_mode[64];
	bool io_mode_set;

	unsigned int lstmode;
	bool lstmode_set;

	unsigned int showmode;
	bool showmode_set;

	unsigned int ibnbdmode;
	bool ibnbdmode_set;

	const char *access_mode;
	bool access_mode_set;

	struct table_column *clms_devices_clt[CLM_MAX_CNT];
	struct table_column *clms_devices_srv[CLM_MAX_CNT];

	struct table_column *clms_sessions_clt[CLM_MAX_CNT];
	struct table_column *clms_sessions_srv[CLM_MAX_CNT];

	struct table_column *clms_paths_clt[CLM_MAX_CNT];
	struct table_column *clms_paths_srv[CLM_MAX_CNT];

	bool notree_set;
	bool noterm_set;
	bool help_set;
	bool verbose_set;

	int unit_id;
	bool unit_set;
	char unit[5];

	int prec;
	bool prec_set;

	bool noheaders_set;
	bool nototals_set;
	bool force_set;

	struct path paths[32]; /* lazy */
	int path_cnt;

	const char *from;
	bool from_set;
};

static struct args args;

int i_to_byte_unit(char *str, size_t len, uint64_t v, int humanize)
{
	if (humanize)
		if (args.unit_set)
			return i_to_str_unit(v, str, len, args.unit_id,
					     args.prec);
		else
			return i_to_str(v, str, len, args.prec);
	else
		return snprintf(str, len, "%" PRIu64, v);
}

int path_to_shortdesc(char *str, size_t len, enum color *clr,
		      void *v, int humanize)
{
	struct ibnbd_path *p = container_of(v, struct ibnbd_path, sess);
	enum color c;

	*clr = CDIM;

	/* return if sess not set (this is the case if called on a total) */
	if (!p->sess)
		return 0;

	if (!strcmp(p->state, "connected"))
		c = CDIM;
	else
		c = CRED;

	return snprintf(str, len, "%s %d %s%s%s", p->hca_name, p->hca_port,
			CLR(trm, c, p->pathname));
}

struct sarg {
	const char *str;
	const char *descr;
	int (*parse)(int argc, char **argv, int i, const struct sarg *sarg);
	void *f;
	int dist;
};

static int parse_fmt(int argc, char **argv, int i, const struct sarg *sarg)
{
	if (!strcasecmp(argv[i], "csv"))
		args.fmt = FMT_CSV;
	else if (!strcasecmp(argv[i], "json"))
		args.fmt = FMT_JSON;
	else if (!strcasecmp(argv[i], "xml"))
		args.fmt = FMT_XML;
	else if (!strcasecmp(argv[i], "term"))
		args.fmt = FMT_TERM;
	else
		return i;

	args.fmt_set = true;

	return i + 1;
}

static int parse_io_mode(int argc, char **argv, int i, const struct sarg *sarg)
{
	if (strcasecmp(argv[i], "blockio") &&
	    strcasecmp(argv[i], "fileio"))
		return i;

	strcpy(args.io_mode, argv[i]);

	args.io_mode_set = true;

	return i + 1;
}

enum lstmode {
	LST_DEVICES,
	LST_SESSIONS,
	LST_PATHS
};

enum showmode {
	SHOW_DEVICE,
	SHOW_SESSION,
	SHOW_PATH
};

static int parse_lst(int argc, char **argv, int i, const struct sarg *sarg)
{
	if (!strcasecmp(argv[i], "devices") ||
	    !strcasecmp(argv[i], "device") ||
	    !strcasecmp(argv[i], "devs") ||
	    !strcasecmp(argv[i], "dev")) {
		args.lstmode = LST_DEVICES;
		args.showmode = SHOW_DEVICE;
	} else if (!strcasecmp(argv[i], "sessions") ||
		 !strcasecmp(argv[i], "session") ||
		 !strcasecmp(argv[i], "sess")) {
		args.lstmode = LST_SESSIONS;
		args.showmode = SHOW_SESSION;
	} else if (!strcasecmp(argv[i], "paths") ||
		 !strcasecmp(argv[i], "path")) {
		args.lstmode = LST_PATHS;
		args.showmode = SHOW_PATH;
	} else
		return i;

	args.lstmode_set = true;
	args.showmode_set = true;

	return i + 1;
}

enum ibnbdmode {
	IBNBD_NONE = 0,
	IBNBD_CLIENT = 1,
	IBNBD_SERVER = 1 << 1,
	IBNBD_BOTH = IBNBD_CLIENT | IBNBD_SERVER,
};

static int parse_from(int argc, char **argv, int i, const struct sarg *sarg)
{
	int j = i + 1;

	if (j >= argc) {
		ERR("Please specify the destionation to map from\n");
		return i;
	}

	args.from = argv[j];
	args.from_set = 1;

	return j + 1;
}

static int parse_mode(int argc, char **argv, int i, const struct sarg *sarg)
{
	if (!strcasecmp(argv[i], "client") || !strcasecmp(argv[i], "clt"))
		args.ibnbdmode = IBNBD_CLIENT;
	else if (!strcasecmp(argv[i], "server") || !strcasecmp(argv[i], "srv"))
		args.ibnbdmode = IBNBD_SERVER;
	else if (!strcasecmp(argv[i], "both"))
		args.ibnbdmode = IBNBD_BOTH;
	else
		return i;

	args.ibnbdmode_set = true;

	return i + 1;
}

static int parse_rw(int argc, char **argv, int i, const struct sarg *sarg)
{
	if (strcasecmp(argv[i], "ro") &&
	    strcasecmp(argv[i], "rw") &&
	    strcasecmp(argv[i], "migration"))
		return i;

	args.access_mode = argv[i];
	args.access_mode_set = true;

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

static int parse_unit(int argc, char **argv, int i, const struct sarg *sarg)
{
	int rc;

	rc = get_unit_index(sarg->str, &args.unit_id);
	assert(rc == 0);

	clm_set_hdr_unit(&clm_ibnbd_dev_rx_sect, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_dev_tx_sect, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_sess_rx_bytes, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_sess_tx_bytes, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_path_rx_bytes, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_path_tx_bytes, sarg->descr);

	args.unit_set = true;
	return i + 1;
}

static int parse_all(int argc, char **argv, int i, const struct sarg *sarg)
{
	memcpy(&args.clms_devices_clt, &all_clms_devices_clt,
	       ARRSIZE(all_clms_devices_clt) * sizeof(all_clms_devices[0]));
	memcpy(&args.clms_devices_srv, &all_clms_devices_srv,
	       ARRSIZE(all_clms_devices_srv) * sizeof(all_clms_devices[0]));
	memcpy(&args.clms_sessions_clt, &all_clms_sessions_clt,
	       ARRSIZE(all_clms_sessions_clt) * sizeof(all_clms_sessions[0]));
	memcpy(&args.clms_sessions_srv, &all_clms_sessions_srv,
	       ARRSIZE(all_clms_sessions_srv) * sizeof(all_clms_sessions[0]));
	memcpy(&args.clms_paths_clt, &all_clms_paths_clt,
	       ARRSIZE(all_clms_paths_clt) * sizeof(all_clms_paths[0]));
	memcpy(&args.clms_paths_srv, &all_clms_paths_srv,
	       ARRSIZE(all_clms_paths_srv) * sizeof(all_clms_paths[0]));


	return i + 1;
}

static int parse_flag(int argc, char **argv, int i, const struct sarg *sarg)
{
	*(short *)sarg->f = 1;

	return i + 1;
}

static struct sarg sargs[] = {
	{"from", "Destination to map a device from", parse_from, NULL},
	{"client", "Information for client", parse_mode, NULL},
	{"clt", "Information for client", parse_mode, NULL},
	{"server", "Information for server", parse_mode, NULL},
	{"srv", "Information for server", parse_mode, NULL},
	{"both", "Information for both", parse_mode, NULL},
	{"devices", "List mapped devices", parse_lst, NULL},
	{"device", "", parse_lst, NULL},
	{"devs", "", parse_lst, NULL},
	{"dev", "", parse_lst, NULL},
	{"sessions", "List sessions", parse_lst, NULL},
	{"session", "", parse_lst, NULL},
	{"sess", "", parse_lst, NULL},
	{"paths", "List paths", parse_lst, NULL},
	{"path", "", parse_lst, NULL},
	{"notree", "Don't display paths for each sessions", parse_flag,
		&args.notree_set},
	{"xml", "Print in XML format", parse_fmt, NULL},
	{"csv", "Print in CSV format", parse_fmt, NULL},
	{"json", "Print in JSON format", parse_fmt, NULL},
	{"term", "Print for terminal", parse_fmt, NULL},
	{"ro", "Readonly", parse_rw, NULL},
	{"rw", "Writable", parse_rw, NULL},
	{"migration", "Writable (migration)", parse_rw, NULL},
	{"blockio", "Block IO mode", parse_io_mode, NULL},
	{"fileio", "File IO mode", parse_io_mode, NULL},
	{"help", "Display help and exit", parse_flag, &args.help_set},
	{"verbose", "Verbose output", parse_flag, &args.verbose_set},
	{"-v", "Verbose output", parse_flag, &args.verbose_set},
	{"B", "Byte", parse_unit, NULL},
	{"K", "KiB", parse_unit, NULL},
	{"M", "MiB", parse_unit, NULL},
	{"G", "GiB", parse_unit, NULL},
	{"T", "TiB", parse_unit, NULL},
	{"P", "PiB", parse_unit, NULL},
	{"E", "EiB", parse_unit, NULL},
	{"noheaders", "Don't print headers", parse_flag, &args.noheaders_set},
	{"nototals", "Don't print totals", parse_flag, &args.nototals_set},
	{"force", "Force operation", parse_flag, &args.force_set},
	{"noterm", "Non-interactive mode", parse_flag, &args.noterm_set},
	{"-f", "", parse_flag, &args.force_set},
	{"all", "Print all columns", parse_all, NULL},
	{0}
};

static struct sarg *find_sarg(char *str, const struct sarg *sargs)
{
	do {
		if (!strcasecmp(str, (*sargs).str))
			return (struct sarg *)sargs;
	} while ((*++sargs).str);

	return NULL;
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
	struct sarg *s;

	s = find_sarg(str, sargs);
	if (s)
		print_opt(s->str, s->descr);
}

struct cmd {
	const char *cmd;
	const char *short_d;
	const char *long_d;
	int (*func)(void);
	int (*parse_args)(int argc, char **args, int i);
	void (*help)(struct cmd *cmd);
	int dist;
};

static struct cmd *find_cmd(char *cmd, const struct cmd *cmds)
{
	do {
		if (!strcmp(cmd, (*cmds).cmd))
			return (struct cmd *)cmds;
	} while ((*++cmds).cmd);

	return NULL;
}

static void print_usage(const char *program_name, const struct cmd *cmds)
{
	printf("Usage: %s%s%s {", CLR(trm, CBLD, program_name));

	clr_print(trm, CBLD, "%s", (*cmds).cmd);

	while ((*++cmds).cmd)
		printf("|%s%s%s", CLR(trm, CBLD, (*cmds).cmd));

	printf("} [ARGUMENTS]\n");
}

static void print_help(const char *program_name, const struct cmd *cmds)
{
	print_usage(program_name, cmds);
	printf("\nIBNBD command line utility.\n");
	printf("\nSubcommands:\n");
	do
		printf("     %-*s%s\n", 20, (*cmds).cmd, (*cmds).short_d);
	while ((*++cmds).cmd);

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int cmd_help(void);

static void cmd_print_usage(struct cmd *cmd, const char *a)
{
	printf("Usage: %s%s%s %s%s%s %s[OPTIONS]\n",
	       CLR(trm, CBLD, args.pname), CLR(trm, CBLD, cmd->cmd), a);
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
		  "Comma separated list of fields to be printed."
		  " The list can be");
	print_opt("", "prefixed with '+' or '-' to add or remove fields"
		      " from the ");
	print_opt("", "default selection.\n");
}

static void print_fields(struct table_column **def_clt,
			 struct table_column **def_srv,
			 struct table_column **all)
{
	table_tbl_print_term(HPRE, all, trm);
	printf("\n%sDefault client: ", HPRE);
	print_clms_list(def_clt);
	printf("%sDefault server: ", HPRE);
	print_clms_list(def_srv);
	printf("\n");
}

static void help_list(struct cmd *cmd)
{
	cmd_print_usage(cmd, "");

	printf("\nOptions:\n");
	print_opt("{mode}", "Information to print: devices|sessions|paths.");
	print_opt("", "Default: devices.");
	help_fields();

	printf("%s%s%s%s\n", HPRE, CLR(trm, CDIM, "Device Fields"));
	print_fields(def_clms_devices_clt, def_clms_devices_srv,
		     all_clms_devices);

	printf("%s%s%s%s\n", HPRE, CLR(trm, CDIM, "Session Fields"));
	print_fields(def_clms_sessions_clt, def_clms_sessions_srv,
		     all_clms_sessions);

	printf("%s%s%s%s\n", HPRE, CLR(trm, CDIM, "Path Fields"));
	print_fields(def_clms_paths_clt, def_clms_paths_srv, all_clms_paths);

	printf("\n%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_sarg_descr("notree");
	print_sarg_descr("noheaders");
	print_sarg_descr("nototals");
	print_sarg_descr("help");
}

static int clm_cnt(struct table_column **cs)
{
	int i;

	for (i = 0; cs[i]; i++)
		;

	return i;
}

static int has_num(struct table_column **cs)
{
	int i;

	for (i = 0; cs[i]; i++)
		if (cs[i]->m_type == FLD_NUM)
			return 1;

	return 0;
}

static int list_devices_term(struct ibnbd_sess_dev **sds,
			     struct table_column **cs)
{
	struct ibnbd_dev d_total = {
		.rx_sect = 0,
		.tx_sect = 0
	};
	struct ibnbd_sess_dev total = {
		.dev = &d_total,
		.mapping_path = ""
	};
	struct table_fld *flds;
	int i, cs_cnt, dev_num;

	cs_cnt = clm_cnt(cs);

	for (dev_num = 0; sds[dev_num]; dev_num++)
		;

	if (!has_num(cs))
		args.nototals_set = true;

	flds = calloc((dev_num + 1) * cs_cnt, sizeof(*flds));
	if (!flds) {
		ERR("not enough memory\n");
		return -ENOMEM;
	}

	for (i = 0; sds[i]; i++) {
		table_row_stringify(sds[i], flds + i * cs_cnt, cs, 1, 0);
		total.dev->rx_sect += sds[i]->dev->rx_sect;
		total.dev->tx_sect += sds[i]->dev->tx_sect;
	}

	if (!args.nototals_set)
		table_row_stringify(&total, flds + i * cs_cnt,
				    cs, 1, 0);

	if (!args.noheaders_set)
		table_header_print_term("", cs, trm, 'a');

	for (i = 0; i < dev_num; i++)
		table_flds_print_term("", flds + i * cs_cnt,
				      cs, trm, 0);

	if (!args.nototals_set) {
		table_row_print_line("", cs, trm, 1, 0);
		table_flds_del_not_num(flds + i * cs_cnt, cs);
		table_flds_print_term("", flds + i * cs_cnt,
				      cs, trm, 0);
	}

	free(flds);


	return 0;
}

static void list_devices_csv(struct ibnbd_sess_dev **sds,
			     struct table_column **cs)
{
	int i;

	if (!sds[0])
		return;

	if (!args.noheaders_set)
		table_header_print_csv(cs);

	for (i = 0; sds[i]; i++)
		table_row_print(sds[i], FMT_CSV, "", cs, 0, 0, 0);
}

static void list_devices_json(struct ibnbd_sess_dev **sds,
			      struct table_column **cs)
{
	int i;

	if (!sds[0])
		return;

	printf("[\n");

	for (i = 0; sds[i]; i++) {
		if (i)
			printf(",\n");
		table_row_print(sds[i], FMT_JSON, "\t\t", cs, 0, 0, 0);
	}

	printf("\n\t]");
}

static void list_devices_xml(struct ibnbd_sess_dev **sds,
			     struct table_column **cs)
{
	int i;

	for (i = 0; sds[i]; i++) {
		printf("\t<device>\n");
		table_row_print(sds[i], FMT_XML, "\t\t", cs, 0, 0, 0);
		printf("\t</device>\n");
	}
}

static int list_devices(struct ibnbd_sess_dev **s_clt,
			struct ibnbd_sess_dev **s_srv)
{
	int clt_s_num = 0, srv_s_num = 0;

	if (args.ibnbdmode & IBNBD_CLIENT)
		for (clt_s_num = 0; s_clt[clt_s_num]; clt_s_num++)
			;

	if (args.ibnbdmode & IBNBD_SERVER)
		for (srv_s_num = 0; s_srv[srv_s_num]; srv_s_num++)
			;

	switch (args.fmt) {
	case FMT_CSV:
		if (clt_s_num && srv_s_num)
			printf("Imports:\n");

		if (clt_s_num)
			list_devices_csv(s_clt, args.clms_devices_clt);

		if (clt_s_num && srv_s_num)
			printf("Exports:\n");

		if (srv_s_num)
			list_devices_csv(s_srv, args.clms_devices_srv);

		break;
	case FMT_JSON:
		printf("{\n");

		if (clt_s_num) {
			printf("\t\"imports\": ");
			list_devices_json(s_clt, args.clms_devices_clt);
		}

		if (clt_s_num && srv_s_num)
			printf(",");

		printf("\n");

		if (srv_s_num) {
			printf("\t\"exports\": ");
			list_devices_json(s_srv, args.clms_devices_srv);
		}

		printf("\n}\n");

		break;
	case FMT_XML:
		if (clt_s_num) {
			printf("<imports>\n");
			list_devices_xml(s_clt, args.clms_devices_clt);
			printf("</imports>\n");
		}
		if (srv_s_num) {
			printf("<exports>\n");
			list_devices_xml(s_srv, args.clms_devices_srv);
			printf("</exports>\n");
		}

		break;
	case FMT_TERM:
	default:
		if (clt_s_num && srv_s_num)
			printf("%s%s%s\n", CLR(trm, CDIM, "Imported devices"));

		if (clt_s_num)
			list_devices_term(s_clt, args.clms_devices_clt);

		if (clt_s_num && srv_s_num)
			printf("%s%s%s\n", CLR(trm, CDIM, "Exported devices"));

		if (srv_s_num)
			list_devices_term(s_srv, args.clms_devices_srv);

		break;
	}

	return 0;
}

static int list_paths_term(struct ibnbd_path **paths, int path_cnt,
			   struct table_column **cs, int tree)
{
	struct ibnbd_path total = {
		.rx_bytes = 0,
		.tx_bytes = 0,
		.inflights = 0,
		.reconnects = 0
	};
	int i, cs_cnt, fld_cnt = 0;
	struct table_fld *flds;

	cs_cnt = clm_cnt(cs);

	flds = calloc((path_cnt + 1) * cs_cnt, sizeof(*flds));
	if (!flds) {
		ERR("not enough memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < path_cnt; i++) {
		table_row_stringify(paths[i], flds + fld_cnt, cs, 1, 0);

		fld_cnt += cs_cnt;

		total.rx_bytes += paths[i]->rx_bytes;
		total.tx_bytes += paths[i]->tx_bytes;
		total.inflights += paths[i]->inflights;
		total.reconnects += paths[i]->reconnects;
	}

	if (!args.nototals_set)
		table_row_stringify(&total, flds + fld_cnt, cs, 1, 0);

	if (!args.noheaders_set && !tree)
		table_header_print_term("", cs, trm, 'a');

	fld_cnt = 0;
	for (i = 0; i < path_cnt; i++) {
		table_flds_print_term(
			!tree ? "" : i < path_cnt - 1 ?
			"├─ " : "└─ ", flds + fld_cnt, cs, trm, 0);
		fld_cnt += cs_cnt;
	}

	if (!args.nototals_set && has_num(cs) && !tree) {
		table_row_print_line("", cs, trm, 1, 0);
		table_flds_del_not_num(flds + fld_cnt, cs);
		table_flds_print_term("", flds + fld_cnt, cs, trm, 0);
	}

	free(flds);

	return 0;
}

static int list_sessions_term(struct ibnbd_sess **sessions,
			      struct table_column **cs)
{
	struct ibnbd_sess total = {
		.act_path_cnt = 0,
		.path_cnt = 0,
		.rx_bytes = 0,
		.tx_bytes = 0,
		.inflights = 0,
		.reconnects = 0
	};
	int i, cs_cnt, sess_num;
	struct table_fld *flds;

	cs_cnt = clm_cnt(cs);
	for (sess_num = 0; sessions[sess_num]; sess_num++)
		;

	flds = calloc((sess_num + 1) * cs_cnt, sizeof(*flds));
	if (!flds) {
		ERR("not enough memory\n");
		return -ENOMEM;
	}

	for (i = 0; sessions[i]; i++) {
		table_row_stringify(sessions[i], flds + i * cs_cnt, cs, 1, 0);

		total.act_path_cnt += sessions[i]->act_path_cnt;
		total.path_cnt += sessions[i]->path_cnt;
		total.rx_bytes += sessions[i]->rx_bytes;
		total.tx_bytes += sessions[i]->tx_bytes;
		total.inflights += sessions[i]->inflights;
		total.reconnects += sessions[i]->reconnects;
	}

	if (!args.nototals_set) {
		table_row_stringify(&total, flds + sess_num * cs_cnt,
				    cs, 1, 0);
	}

	if (!args.noheaders_set)
		table_header_print_term("", cs, trm, 'a');

	for (i = 0; sessions[i]; i++) {
		table_flds_print_term("", flds + i * cs_cnt,
				      cs, trm, 0);
		if (!args.notree_set)
			list_paths_term(sessions[i]->paths,
					sessions[i]->path_cnt,
					clms_paths_shortdesc, 1);
	}

	if (!args.nototals_set && has_num(cs)) {
		table_row_print_line("", cs, trm, 1, 0);
		table_flds_del_not_num(flds + sess_num * cs_cnt, cs);
		table_flds_print_term("", flds + sess_num * cs_cnt,
				      cs, trm, 0);
	}

	free(flds);

	return 0;
}

static void list_sessions_csv(struct ibnbd_sess **sessions,
			      struct table_column **cs)
{
	int i;

	if (!args.noheaders_set)
		table_header_print_csv(cs);

	for (i = 0; sessions[i]; i++)
		table_row_print(sessions[i], FMT_CSV, "", cs, 0, 0, 0);
}

static void list_sessions_json(struct ibnbd_sess **sessions,
			       struct table_column **cs)
{
	int i;

	printf("[\n");

	for (i = 0; sessions[i]; i++) {
		if (i)
			printf(",\n");
		table_row_print(sessions[i], FMT_JSON, "\t\t", cs, 0, 0, 0);
	}

	printf("\n\t]");
}

static void list_sessions_xml(struct ibnbd_sess **sessions,
			      struct table_column **cs)
{
	int i;

	for (i = 0; sessions[i]; i++) {
		printf("\t<session>\n");
		table_row_print(sessions[i], FMT_XML, "\t\t", cs, 0, 0, 0);
		printf("\t</session>\n");
	}
}

static int list_sessions(struct ibnbd_sess **s_clt, struct ibnbd_sess **s_srv)
{
	int clt_s_num = 0, srv_s_num = 0;

	if (args.ibnbdmode & IBNBD_CLIENT)
		for (clt_s_num = 0; s_clt[clt_s_num]; clt_s_num++)
			;

	if (args.ibnbdmode & IBNBD_SERVER)
		for (srv_s_num = 0; s_srv[srv_s_num]; srv_s_num++)
			;

	switch (args.fmt) {
	case FMT_CSV:
		if (clt_s_num && srv_s_num)
			printf("Outgoing:\n");

		if (clt_s_num)
			list_sessions_csv(s_clt, args.clms_sessions_clt);

		if (clt_s_num && srv_s_num)
			printf("Incoming:\n");

		if (srv_s_num)
			list_sessions_csv(s_srv, args.clms_sessions_srv);
		break;
	case FMT_JSON:
		printf("{\n");

		if (clt_s_num) {
			printf("\t\"outgoing\": ");
			list_sessions_json(s_clt, args.clms_sessions_clt);
		}

		if (clt_s_num && srv_s_num)
			printf(",");

		printf("\n");

		if (srv_s_num) {
			printf("\t\"incoming\": ");
			list_sessions_json(s_srv, args.clms_sessions_srv);
		}

		printf("\n}\n");

		break;
	case FMT_XML:
		if (clt_s_num) {
			printf("\t\"outgoing\": ");
			printf("<outgoing>\n");
			list_sessions_xml(s_clt, args.clms_sessions_clt);
			printf("</outgoing>\n");
		}
		if (srv_s_num) {
			printf("\t\"outgoing\": ");
			printf("<incoming>\n");
			list_sessions_xml(s_srv, args.clms_sessions_srv);
			printf("</incoming>\n");
		}

		break;
	case FMT_TERM:
	default:
		if (clt_s_num && srv_s_num)
			printf("%s%s%s\n", CLR(trm, CDIM, "Outgoing sessions"));

		if (clt_s_num)
			list_sessions_term(s_clt, args.clms_sessions_clt);

		if (clt_s_num && srv_s_num)
			printf("%s%s%s\n", CLR(trm, CDIM, "Incoming sessions"));

		if (srv_s_num)
			list_sessions_term(s_srv, args.clms_sessions_srv);
		break;
	}

	return 0;
}

static void list_paths_csv(struct ibnbd_path **paths,
			   struct table_column **cs)
{
	int i;

	if (!args.noheaders_set)
		table_header_print_csv(cs);

	for (i = 0; paths[i]; i++)
		table_row_print(paths[i], FMT_CSV, "", cs, 0, 0, 0);
}

static void list_paths_json(struct ibnbd_path **paths,
			    struct table_column **cs)
{
	int i;

	printf("\n\t[\n");

	for (i = 0; paths[i]; i++) {
		if (i)
			printf(",\n");
		table_row_print(paths[i], FMT_JSON, "\t\t", cs, 0, 0, 0);
	}

	printf("\n\t]");
}

static void list_paths_xml(struct ibnbd_path **paths,
			   struct table_column **cs)
{
	int i;

	for (i = 0; paths[i]; i++) {
		printf("\t<path>\n");
		table_row_print(paths[i], FMT_XML, "\t\t", cs, 0, 0, 0);
		printf("\t</path>\n");
	}
}


static int list_paths(struct ibnbd_path **p_clt, struct ibnbd_path **p_srv)
{
	int clt_p_num = 0, srv_p_num = 0;

	if (args.ibnbdmode & IBNBD_CLIENT)
		for (clt_p_num = 0; p_clt[clt_p_num]; clt_p_num++)
			;

	if (args.ibnbdmode & IBNBD_SERVER)
		for (srv_p_num = 0; p_srv[srv_p_num]; srv_p_num++)
			;

	switch (args.fmt) {
	case FMT_CSV:
		if (clt_p_num && srv_p_num)
			printf("Outgoing paths:\n");

		if (clt_p_num)
			list_paths_csv(p_clt, args.clms_paths_clt);

		if (clt_p_num && srv_p_num)
			printf("Incoming paths:\n");

		if (srv_p_num)
			list_paths_csv(p_srv, args.clms_paths_srv);
		break;
	case FMT_JSON:
		printf("{\n");

		if (clt_p_num) {
			printf("\t\"outgoing paths\": ");
			list_paths_json(p_clt, args.clms_paths_clt);
		}

		if (clt_p_num && srv_p_num)
			printf(",");

		printf("\n");

		if (srv_p_num) {
			printf("\t\"incoming paths\": ");
			list_paths_json(p_srv, args.clms_paths_srv);
		}

		printf("\n}\n");

		break;
	case FMT_XML:
		if (clt_p_num) {
			printf("<outgoing paths>\n");
			list_paths_xml(p_clt, args.clms_paths_clt);
			printf("</outgoing paths>\n");
		}
		if (srv_p_num) {
			printf("<incoming paths>\n");
			list_paths_xml(p_srv, args.clms_paths_srv);
			printf("</incoming paths>\n");
		}

		break;
	case FMT_TERM:
	default:
		if (clt_p_num && srv_p_num)
			printf("%s%s%s\n", CLR(trm, CDIM, "Outgoing paths"));

		if (clt_p_num)
			list_paths_term(p_clt, clt_p_num,
					args.clms_paths_clt, 0);

		if (clt_p_num && srv_p_num)
			printf("%s%s%s\n", CLR(trm, CDIM, "Incoming paths"));

		if (srv_p_num)
			list_paths_term(p_srv, srv_p_num,
					args.clms_paths_srv, 0);
		break;
	}

	return 0;
}

static int cmd_list(void)
{
	int rc;

	switch (args.lstmode) {
	case LST_DEVICES:
	default:
		rc = list_devices(sds_clt, sds_srv);
		break;
	case LST_SESSIONS:
		rc = list_sessions(sess_clt, sess_srv);
		break;
	case LST_PATHS:
		rc = list_paths(paths_clt, paths_srv);
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

/*
 * Find first ibnbd device by device name, path or mapping path
 */
static bool device_exists(const char *name)
{
	int i;

	for (i = 0; sds_clt[i]; i++)
		if (match_device(sds_clt[i], name))
			return true;
	for (i = 0; sds_srv[i]; i++)
		if (match_device(sds_srv[i], name))
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
static int find_devices_all(const char *name, struct ibnbd_sess_dev **ds_imp,
			    struct ibnbd_sess_dev **ds_exp)
{
	int cnt = 0;

	if (args.ibnbdmode & IBNBD_CLIENT)
		cnt += find_devices(name, sds_clt, ds_imp);
	if (args.ibnbdmode & IBNBD_SERVER)
		cnt += find_devices(name, sds_srv, ds_exp);

	return cnt;
}

static int show_device(const char *devname)
{
	struct ibnbd_sess_dev **ds_imp, **ds_exp, **ds;
	struct table_fld flds[CLM_MAX_CNT];
	struct table_column **cs;
	int cnt, ret = 0;

	for (cnt = 0; sds_clt[cnt]; cnt++);
	ds_imp = calloc(cnt, sizeof(*ds_imp));
	if (cnt && !ds_imp) {
		ERR("Failed to alloc memory\n");
		return -ENOMEM;
	}

	for (cnt = 0; sds_srv[cnt]; cnt++);
	ds_exp = calloc(cnt, sizeof(*ds_exp));
	if (cnt && !ds_exp) {
		ERR("Failed to alloc memory\n");
		ret = -ENOMEM;
		goto free_imp;
	}

	cnt = find_devices_all(devname, ds_imp, ds_exp);
	if (!cnt) {
		ERR("Found no devices matching '%s'\n", devname);
		ret = -ENOENT;
		goto free_exp;
	}
	if (cnt > 1) {
		INF("There are multiple devices matching '%s':\n", devname);
		ret = list_devices(ds_imp, ds_exp);
		goto free_exp;
	}
	if (ds_imp[0]) {
		ds = ds_imp;
		cs = args.clms_devices_clt;
	} else {
		ds = ds_exp;
		cs = args.clms_devices_srv;
	}

	switch (args.fmt) {
	case FMT_CSV:
		list_devices_csv(ds, cs);
		break;
	case FMT_JSON:
		list_devices_json(ds, cs);
		printf("\n");
		break;
	case FMT_XML:
		list_devices_xml(ds, cs);
		break;
	case FMT_TERM:
	default:
		table_row_stringify(ds[0], flds, cs, 1, 0);
		table_entry_print_term("", flds, cs, table_get_max_h_width(cs),
				       trm);
		break;
	}

free_exp:
	free(ds_exp);
free_imp:
	free(ds_imp);
	return ret;
}

static bool match_sess(struct ibnbd_sess *s, const char *name)
{
	char *at;

	if (!strcmp(name, s->sessname))
		return true;

	at = strchr(s->sessname, '@');
	if (at && (!strcmp(name, at + 1) ||
		   !strncmp(name, s->sessname, strlen(name))))
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

static bool session_match_exists(const char *name)
{
	int i;

	for (i = 0; sess_clt[i]; i++)
		if (match_sess(sess_clt[i], name))
			return true;
	for (i = 0; sess_srv[i]; i++)
		if (match_sess(sess_srv[i], name))
			return true;

	return false;
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

static int find_sessions_all(const char *name, struct ibnbd_sess **ss_clt,
			     struct ibnbd_sess **ss_srv)
{
	int cnt = 0;

	if (args.ibnbdmode & IBNBD_CLIENT)
		cnt += find_sessions_match(name, sess_clt, ss_clt);
	if (args.ibnbdmode & IBNBD_SERVER)
		cnt += find_sessions_match(name, sess_srv, ss_srv);


	return cnt;
}

static bool match_path(struct ibnbd_path *p, const char *name)
{
	int port;
	char *at;

	if (!strcmp(p->pathname, name))
		return true;
	if (!strcmp(name, p->sess->sessname))
		return true;
	if (!strcmp(name, p->src_addr))
		return true;
	if (!strcmp(name, p->dst_addr))
		return true;
	if (sscanf(name, "%d\n", &port) == 1 &&
	    p->hca_port == port)
		return true;
	if (!strcmp(name, p->hca_name))
		return true;

	at = strrchr(name, ':');
	if (at) {
		if (strncmp(p->sess->sessname, name,
			    strlen(p->sess->sessname)))
			return false;

		if (sscanf(at + 1, "%d\n", &port) == 1 && p->hca_port == port)
			return true;
		if (!strcmp(at + 1, p->dst_addr))
			return true;
		if (!strcmp(at + 1, p->src_addr))
			return true;
		if (!strcmp(at + 1, p->hca_name))
			return true;
	}

	return false;
}

static bool path_exists(const char *name)
{
	int i;

	for (i = 0; paths_clt[i]; i++)
		if (match_path(paths_clt[i], name))
			return true;
	for (i = 0; paths_srv[i]; i++)
		if (match_path(paths_srv[i], name))
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
			  struct ibnbd_path **pp_srv)
{
	int cnt = 0;

	if (args.ibnbdmode & IBNBD_CLIENT)
		cnt += find_paths(name, paths_clt, pp_clt);
	if (args.ibnbdmode & IBNBD_SERVER)
		cnt += find_paths(name, paths_srv, pp_srv);

	return cnt;
}


static int get_showmode(const char *name, enum showmode *mode)
{
	if (device_exists(name))
		*mode = SHOW_DEVICE;
	else if (session_match_exists(name))
		*mode = SHOW_SESSION;
	else if (path_exists(name))
		*mode = SHOW_PATH;
	else
		return -ENOENT;

	return 0;
}

static int show_path(const char *pathname)
{
	struct ibnbd_path **pp_clt, **pp_srv, **pp;
	struct table_fld flds[CLM_MAX_CNT];
	struct table_column **cs;
	int cnt, res = 0;

	for (cnt = 0; paths_clt[cnt]; cnt++);
	pp_clt = calloc(cnt, sizeof(*pp_clt));
	if (cnt && !pp_clt) {
		ERR("Failed to alloc memory\n");
		return -ENOMEM;
	}

	for (cnt = 0; paths_srv[cnt]; cnt++);
	pp_srv = calloc(cnt, sizeof(*pp_srv));
	if (cnt && !pp_srv) {
		ERR("Failed to alloc memory\n");
		res = -ENOMEM;
		goto free_clt;
	}

	cnt = find_paths_all(pathname, pp_clt, pp_srv);
	if (!cnt) {
		ERR("Found no paths matching '%s'\n", pathname);
		res = -ENOENT;
		goto free_srv;
	}
	if (cnt > 1) {
		INF("There are multiple paths matching '%s':\n", pathname);
		res = list_paths(pp_clt, pp_srv);
		goto free_srv;
	}
	if (pp_clt[0]) {
		pp = pp_clt;
		cs = args.clms_paths_clt;
	} else {
		pp = pp_srv;
		cs = args.clms_paths_srv;
	}

	switch (args.fmt) {
	case FMT_CSV:
		list_paths_csv(pp, cs);
		break;
	case FMT_JSON:
		list_paths_json(pp, cs);
		printf("\n");
		break;
	case FMT_XML:
		list_paths_xml(pp, cs);
		break;
	case FMT_TERM:
	default:
		table_row_stringify(pp[0], flds, cs, 1, 0);
		table_entry_print_term("", flds, cs,
				       table_get_max_h_width(cs), trm);
		break;
	}
free_srv:
	free(pp_srv);
free_clt:
	free(pp_clt);
	return res;
}

static int show_session(const char *sessname)
{
	struct ibnbd_sess **ss_clt, **ss_srv, **ss;
	struct table_fld flds[CLM_MAX_CNT];
	struct table_column **cs, **ps;
	int cnt, res = 0;

	for (cnt = 0; sess_clt[cnt]; cnt++);
	ss_clt = calloc(cnt, sizeof(*ss_clt));
	if (!ss_clt) {
		ERR("Failed to alloc memory\n");
		return -ENOMEM;
	}

	for (cnt = 0; sess_srv[cnt]; cnt++);
	ss_srv = calloc(cnt, sizeof(*ss_srv));
	if (!ss_srv) {
		ERR("Failed to alloc memory\n");
		res = -ENOMEM;
		goto free_clt;
	}

	cnt = find_sessions_all(sessname, ss_clt, ss_srv);
	if (!cnt) {
		ERR("Found no sessions matching '%s'\n", sessname);
		res = -ENOENT;
		goto free_srv;
	}
	if (cnt > 1) {
		INF("There are multiple sessions matching '%s':\n", sessname);
		res = list_sessions(ss_clt, ss_srv);
		goto free_srv;
	}
	if (ss_clt[0]) {
		ss = ss_clt;
		cs = args.clms_sessions_clt;
		ps = clms_paths_sess_clt;
	} else {
		ss = ss_srv;
		cs = args.clms_sessions_srv;
		ps = clms_paths_sess_srv;
	}

	switch (args.fmt) {
	case FMT_CSV:
		list_sessions_csv(ss, cs);
		break;
	case FMT_JSON:
		list_sessions_json(ss, cs);
		printf("\n");
		break;
	case FMT_XML:
		list_sessions_xml(ss, cs);
		break;
	case FMT_TERM:
	default:
		table_row_stringify(ss[0], flds, cs, 1, 0);
		table_entry_print_term("", flds, cs,
				       table_get_max_h_width(cs), trm);
		printf("%s%s%s", CLR(trm, CBLD, ss[0]->sessname));
		if (ss[0]->side == IBNBD_CLT)
			printf(" %s(%s)%s", CLR(trm, CBLD, ss[0]->mp_short));
		printf("\n");
		list_paths_term(ss[0]->paths, ss[0]->path_cnt, ps, 1);

		break;
	}
free_srv:
	free(ss_srv);
free_clt:
	free(ss_clt);

	return res;
}

static int cmd_show(void)
{
	int rc;

	if (!args.showmode_set) {
		//FIXME
		if (get_showmode(args.name, &args.showmode)) {
			ERR("'%s' is neither device nor session nor path\n",
			    args.name);
			return -ENOENT;
		}
		args.showmode_set = true;
	}

	switch (args.showmode) {
	case SHOW_DEVICE:
		rc = show_device(args.name);
		break;
	case SHOW_SESSION:
		rc = show_session(args.name);
		break;
	case SHOW_PATH:
		rc = show_path(args.name);
		break;
	default:
		assert(0);
		break;
	}

	return rc;
}

static int parse_name(int argc, char **argv, int i)
{
	int j = i + 1;

	if (j >= argc) {
		ERR("Please specify the <name> argument\n");
		return i;
	}

	args.name = argv[j];

	return j + 1;
}

static void help_show(struct cmd *cmd)
{
	cmd_print_usage(cmd, "<name> [path] ");

	printf("\nArguments:\n");
	print_opt("<name>",
		  "Name of a local or a remote block device,"
		  " session name, path name or remote hostname.");
	print_opt("", "I.e. ibnbd0, /dev/ibnbd0,"
		      " d12aef94-4110-4321-9373-3be8494a557b,"
		      " ps401a-1@st401b-2, st401b-2, <ip1>@<ip2>, etc.");
	print_opt("", "In order to display path information,"
		      " path name or identifier");
	print_opt("", "has to be provided, i.e. st401b-2:1.");

	printf("\nOptions:\n");
	help_fields();

	printf("%s%s%s%s\n", HPRE, CLR(trm, CDIM, "Device Fields"));
	print_fields(def_clms_devices_clt, def_clms_devices_srv,
		     all_clms_devices);

	printf("%s%s%s%s\n", HPRE, CLR(trm, CDIM, "Sessions Fields"));
	print_fields(def_clms_sessions_clt, def_clms_sessions_srv,
		     all_clms_sessions);

	printf("%s%s%s%s\n", HPRE, CLR(trm, CDIM, "Paths Fields"));
	print_fields(def_clms_paths_clt, def_clms_paths_srv, all_clms_paths);

	printf("\n%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_opt("{mode}", "Information to print: device|session|path."
			    " Default: device.");

	print_sarg_descr("help");
}

static void help_map(struct cmd *cmd)
{
	cmd_print_usage(cmd, "<path> from <server> ");

	printf("\nArguments:\n");
	print_opt("<device>", "Path to the device to be mapped on server side");
	print_opt("from <server>",
		  "Address, hostname or session name of the server");

	printf("\nOptions:\n");
	print_opt("<path>", "Path(s) to establish: [src_addr@]dst_addr");
	print_opt("", "Address is [ip:]<ipv4>, [ip:]<ipv6> or gid:<gid>");

	print_opt("{io_mode}", "IO Mode to use on server side: fileio|blockio."
		  " Default: blockio");
	print_opt("{rw}", "Access permission on server side: ro|rw|migration."
		  " Default: rw");
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

	args.paths[args.path_cnt].src = src;
	args.paths[args.path_cnt].dst = dst;

	args.path_cnt++;

	return 0;
}

static int cmd_map(void)
{
	char cmd[4096], sessname[NAME_MAX];
	struct ibnbd_sess *sess;
	int i, cnt = 0, ret;

	if (!parse_path(args.from)) {
		/* user provided only paths to establish
		 * -> generate sessname
		 */
		strcpy(sessname, "clt@srv"); /* TODO */
	} else
		strcpy(sessname, args.from);

	sess = find_session(sessname, sess_clt);

	if (!sess && !args.path_cnt) {
		ERR("Client session '%s' not found. Please provide"
		    " at least one path to establish a new one.\n",
		    args.from);
		return -EINVAL;
	}

	if (sess && args.path_cnt)
		INF("Session '%s' exists. Provided paths will be ignored"
		    " by the driver. Please use addpath to add a path to"
		    " an existsing sesion.\n", args.from);

	cnt = snprintf(cmd, sizeof(cmd), "sessname=%s", sessname);
	cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt, " device_path=%s",
			args.name);

	for (i = 0; i < args.path_cnt; i++)
		cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt, " path=%s@%s",
				args.paths[i].src, args.paths[i].dst);

	if (sess)
		for (i = 0; i < sess->path_cnt; i++)
			cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt,
					" path=%s@%s", sess->paths[i]->src_addr,
					sess->paths[i]->dst_addr);

	if (args.io_mode_set)
		cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt, " io_mode=%s",
				args.io_mode);

	if (args.access_mode_set)
		cnt += snprintf(cmd + cnt, sizeof(cmd) - cnt, " access_mode=%s",
				args.access_mode);

	errno = 0;
	ret = printf_sysfs(PATH_IBNBD_CLT, "map_device", "%s", cmd);
	ret = (ret < 0 ? ret : errno);
	if (ret)
		ERR("Failed to map device: %m (%d)\n", ret);

	return ret;
}

static int cmd_resize(void)
{
	struct ibnbd_sess_dev **ds, *bla[1];
	int cnt;

	for (cnt = 0; sds_clt[cnt]; cnt++);
	ds = calloc(cnt, sizeof(*ds));
	if (cnt && !ds) {
		ERR("Failed to alloc memory\n");
		return -ENOMEM;
	}

	bla[0] = NULL;
	cnt = find_devices(args.name, ds, bla);
	if (!cnt) {
		ERR("Device %s not found\n", args.name);
		return -ENOENT;
	}
	if (cnt > 1) {
		ERR("Please specify an exact path. There are multiple devices"
		    " matching %s:\n", args.name);
		list_devices(ds, bla);
		return -EINVAL;
	}

	printf(">>>>>> resize %s to %lu\n", ds[0]->dev->devname, args.size_sect);
	return 0;
}

static void help_resize(struct cmd *cmd)
{
	cmd_print_usage(cmd, "<device name or path or mapping path> ");

	printf("\nArguments:\n");
	print_opt("<device>", "Name of the device to be unmapped");
	print_opt("<size>", "New size of the device");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static void help_unmap(struct cmd *cmd)
{
	cmd_print_usage(cmd, "<device name or path or mapping path> ");

	printf("\nArguments:\n");
	print_opt("<device>", "Name of the device to be unmapped");

	printf("\nOptions:\n");
	print_sarg_descr("force");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int cmd_unmap(void)
{
	printf("TODO\n");
	return 0;
}

static void help_remap(struct cmd *cmd)
{
	cmd_print_usage(cmd, "<devname|sessname> ");

	printf("\nArguments:\n");
	print_opt("<identifier>",
		  "Identifier of a device to be remapped. Or"
		  " identifier of a session to remap all devices on.");

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

static void help_reconnect(struct cmd *cmd)
{
	cmd_print_usage(cmd, "<path or session> ");

	printf("\nArguments:\n");
	print_opt("<identifier>",
		  "Name or identifier of a session or of a path:");
	print_opt("", "[sessname], [pathname], [sessname:port], etc.");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int cmd_reconnect(void)
{
	printf("TODO\n");
	return 0;
}

static void help_disconnect(struct cmd *cmd)
{
	cmd_print_usage(cmd, "<path or session> ");

	printf("\nArguments:\n");
	print_opt("<identifier>",
		  "Name or identifier of a session or of a path:");
	print_opt("", "[sessname], [pathname], [sessname:port], etc.");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int cmd_disconnect(void)
{
	printf("TODO\n");
	return 0;
}

static void help_addpath(struct cmd *cmd)
{
	cmd_print_usage(cmd, "<session> <path> ");

	printf("\nArguments:\n");
	print_opt("<session>",
		  "Name of the session to add the new path to");
	print_opt("<path>",
		  "Path to be added: [src_addr,]dst_addr");
	print_opt("", "Address is of the form ip:<ipv4>, ip:<ipv6> or"
		      " gid:<gid>");

	printf("\nOptions:\n");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int cmd_addpath(void)
{
	printf("TODO\n");
	return 0;
}

static void help_help(struct cmd *cmd);

static void help_delpath(struct cmd *cmd)
{
	cmd_print_usage(cmd, "<path> ");

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

static struct cmd cmds[] = {
	{"list",
		"List block device or transport related information",
		"List block device or transport related information: "
		"devices, sessions, paths, etc.",
		cmd_list, NULL, help_list},
	{"show",
		"Show information about a device, a session or a path",
		"Show information about an ibnbd block- or transport- item: "
		"device, session or path.",
		cmd_show, parse_name, help_show},
	{"map",
		"Map a device from a given server",
		"Map a device from a given server",
		 cmd_map, parse_name, help_map},
	{"resize",
		"Resize a mapped device",
		"Change size of a mapped device",
		 cmd_resize, parse_name, help_resize},
	{"unmap",
		"Unmap an imported device",
		"Umap a given imported device",
		cmd_unmap, parse_name, help_unmap},
	{"remap",
		"Remap a device or all devices on a session",
		"Unmap and map again an imported device or do this "
		"for all devices of a given session",
		 cmd_remap, parse_name, help_remap},
	{"disconnect",
		"Disconnect a path or a session",
		"Disconnect a path or all paths on a given session",
		cmd_disconnect, parse_name, help_disconnect},
	{"reconnect",
		"Reconnect a path or a session",
		"Disconnect and connect again a path or a whole session",
		 cmd_reconnect, parse_name, help_reconnect},
	{"addpath",
		"Add a path to an existing session",
		"Add a new path to an existing session",
		 cmd_addpath, parse_name, help_addpath},
	{"delpath",
		"Delete a path",
		"Delete a given path from the corresponding session",
		 cmd_delpath, parse_name, help_delpath},
	{"help",
		"Display help",
		"Display help message and exit.",
		cmd_help, NULL, help_help},
	{ 0 }
};

static void help_help(struct cmd *cmd)
{
	print_help(args.pname, cmds);
}

static int cmd_help(void)
{
	print_help(args.pname, cmds);
	return 0;
}

static int levenstein_compare(int d1, int d2, const char *s1, const char *s2)
{
	return d1 != d2 ? d1 - d2 : strcmp(s1, s2);
}

static int cmd_compare(const void *p1, const void *p2)
{
	const struct cmd *c1 = p1;
	const struct cmd *c2 = p2;

	return levenstein_compare(c1->dist, c2->dist, c1->cmd, c2->cmd);
}

static int sarg_compare(const void *p1, const void *p2)
{
	const struct sarg *c1 = p1;
	const struct sarg *c2 = p2;

	return levenstein_compare(c1->dist, c2->dist, c1->str, c2->str);
}

static void handle_unknown_cmd(char *cmd, struct cmd *cmds)
{
	struct cmd *cs;
	size_t len = 0, cnt = 0;

	for (cs = cmds; cs->cmd; cs++) {
		cs->dist = levenshtein(cs->cmd, cmd, 0, 2, 1, 3) + 1;
		if (strlen(cs->cmd) < 2)
			cs->dist += 3;
		len++;
		if (cs->dist < 7)
			cnt++;
	}

	if (!cnt)
		return;

	qsort(cmds, len, sizeof(*cmds), cmd_compare);

	printf("Did you mean:\n");

	for (len = 0; len < cnt; len++)
		printf("\t%s\n", cmds[len].cmd);
}

static void handle_unknown_sarg(char *sarg, struct sarg *sargs)
{
	struct sarg *cs;
	size_t len = 0, cnt = 0, i;

	for (cs = sargs; cs->str; cs++) {
		cs->dist = levenshtein(cs->str, sarg, 0, 2, 1, 3) + 1;
		if (strlen(cs->str) < 2)
			cs->dist += 10;
		len++;
		if (cs->dist < 5)
			cnt++;
	}

	if (!cnt)
		return;

	qsort(sargs, len, sizeof(*sargs), sarg_compare);

	printf("Did you mean:\n");

	for (i = 0; i < cnt; i++)
		printf("\t%s\n", sargs[i].str);
}

static int parse_precision(char *str)
{
	unsigned int prec;
	char e;

	if (strncmp(str, "prec", 4))
		return -EINVAL;

	if (sscanf(str + 4, "%u%c\n", &prec, &e) != 1)
		return -EINVAL;

	args.prec = prec;
	args.prec_set = true;

	return 0;
}

static int parse_devices_clms(const char *arg)
{
	int rc_clt, rc_srv;

	rc_clt = table_extend_columns(arg, ",", all_clms_devices_clt,
				      args.clms_devices_clt, CLM_MAX_CNT);

	rc_srv = table_extend_columns(arg, ",", all_clms_devices_srv,
				      args.clms_devices_srv, CLM_MAX_CNT);

	return rc_clt && rc_srv;
}

static int parse_sessions_clms(const char *arg)
{
	int rc_clt, rc_srv;

	rc_clt = table_extend_columns(arg, ",", all_clms_sessions_clt,
				      args.clms_sessions_clt, CLM_MAX_CNT);

	rc_srv = table_extend_columns(arg, ",", all_clms_sessions_srv,
				      args.clms_sessions_srv, CLM_MAX_CNT);
	return rc_clt && rc_srv;
}

static int parse_paths_clms(const char *arg)
{
	int rc_clt, rc_srv;

	rc_clt = table_extend_columns(arg, ",", all_clms_paths_clt,
				      args.clms_paths_clt, CLM_MAX_CNT);

	rc_srv = table_extend_columns(arg, ",", all_clms_paths_srv,
				      args.clms_paths_srv, CLM_MAX_CNT);
	return rc_clt && rc_srv;
}

static int parse_sign(char s)
{
	if (s == '+')
		args.sign = 1;
	else if (s == '-')
		args.sign = -1;
	else
		args.sign = 0;

	return args.sign;
}

static int parse_size(char *str)
{
	uint64_t size;

	if (parse_sign(*str))
		str++;

	if (str_to_size(str, &size))
		return -EINVAL;

	args.size_sect = size >> 9;
	args.size_set = 1;

	return 0;
}

static void init_args(void)
{
	memcpy(&args.clms_devices_clt, &def_clms_devices_clt,
	       ARRSIZE(def_clms_devices_clt) * sizeof(all_clms_devices[0]));
	memcpy(&args.clms_devices_srv, &def_clms_devices_srv,
	       ARRSIZE(def_clms_devices_srv) * sizeof(all_clms_devices[0]));

	memcpy(&args.clms_sessions_clt, &def_clms_sessions_clt,
	       ARRSIZE(def_clms_sessions_clt) * sizeof(all_clms_sessions[0]));
	memcpy(&args.clms_sessions_srv, &def_clms_sessions_srv,
	       ARRSIZE(def_clms_sessions_srv) * sizeof(all_clms_sessions[0]));

	memcpy(&args.clms_paths_clt, &def_clms_paths_clt,
	       ARRSIZE(def_clms_paths_clt) * sizeof(all_clms_paths[0]));
	memcpy(&args.clms_paths_srv, &def_clms_paths_srv,
	       ARRSIZE(def_clms_paths_srv) * sizeof(all_clms_paths[0]));
}

static void default_args(void)
{
	if (!args.lstmode_set)
		args.lstmode = LST_DEVICES;

	if (!args.fmt_set)
		args.fmt = FMT_TERM;

	if (!args.prec_set)
		args.prec = 3;

	if (!args.ibnbdmode_set) {
		if (sess_clt[0])
			args.ibnbdmode |= IBNBD_CLIENT;
		if (sess_srv[0])
			args.ibnbdmode |= IBNBD_SERVER;
	}
}

int main(int argc, char **argv)
{
	int ret = 0, i, rcd, rcs, rcp;
	struct sarg *sarg;
	struct cmd *cmd;

	trm = (isatty(STDOUT_FILENO) == 1);

	init_args();

	args.pname = argv[0];

	if (argc < 2) {
		ERR("no command specified\n");
		print_usage(argv[0], cmds);
		ret = -EINVAL;
		goto out;
	}

	i = 1;

	/*
	 * try finding sess/devs/paths preceding the command
	 * (for those who is used to type ibnbd dev map or ibnbd session list)
	 */
	i = parse_lst(argc, argv, i, NULL);
	/*
	 * try finding clt/srv preceding the command
	 * (for those who is used to type ibnbd clt list or ibnbd srv sess list)
	 */
	i = parse_mode(argc, argv, i, NULL);

	cmd = find_cmd(argv[i], cmds);
	if (!cmd) {
		printf("'%s' is not a valid command. Try '%s%s%s %s%s%s'\n",
		       argv[i], CLR(trm, CBLD, argv[0]),
		       CLR(trm, CBLD, "help"));
		handle_unknown_cmd(argv[i], cmds);
		ret = -EINVAL;
		goto out;
	}
	if (cmd == find_cmd("help", cmds))
		args.help_set = true;

	if (i + 1 < argc && cmd->help &&
	    (!strcmp(argv[i + 1], "help") ||
	     !strcmp(argv[i + 1], "--help") ||
	     !strcmp(argv[i + 1], "-h"))) {
		cmd->help(cmd);
		goto out;
	}

	if (cmd->parse_args) {
		ret = cmd->parse_args(argc, argv, i);
		if (ret == i) {
			if (cmd->help)
				cmd->help(cmd);
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
			rcd = parse_devices_clms(argv[i]);
			rcs = parse_sessions_clms(argv[i]);
			rcp = parse_paths_clms(argv[i]);
			if (!parse_precision(argv[i]) ||
			    !(rcd && rcs && rcp) ||
			    !parse_path(argv[i]) ||
			    !parse_size(argv[i])) {
				i++;
				continue;
			}

			printf("'%s' is not a valid argument. Try '", argv[i]);
			printf("%s%s%s %s%s%s %s%s%s",
			       CLR(trm, CBLD, args.pname),
			       CLR(trm, CBLD, cmd->cmd),
			       CLR(trm, CBLD, "help"));
			printf("'\n");

			handle_unknown_sarg(argv[i], sargs);

			ret = -EINVAL;
			goto out;
		}
		ret = sarg->parse(argc, argv, i, sarg);
		if (i == ret) {
			ret = -EINVAL;
			goto out;
		}
		i = ret;
	}

	ret = ibnbd_sysfs_alloc_all(&sds_clt, &sds_srv, &sess_clt, &sess_srv,
				    &paths_clt, &paths_srv);
	if (ret) {
		ERR("Failed to alloc memory for sysfs entries: %d\n", ret);
		goto out;
	}

	ret = ibnbd_sysfs_read_all(sds_clt, sds_srv, sess_clt, sess_srv,
				   paths_clt, paths_srv);
	if (ret) {
		ERR("Failed to read sysfs entries: %d\n", ret);
		goto free;
	}

	default_args();

	ret = 0;

	if (args.help_set && cmd->help)
		cmd->help(cmd);
	else if (cmd->func) {
		/*if (args.ibnbdmode == IBNBD_NONE) {
			ERR("ibnbd modules not loaded\n");
			ret = -ENOENT;
			goto free;
		}*/
		ret = cmd->func();
	}
free:
	ibnbd_sysfs_free_all(sds_clt, sds_srv, sess_clt, sess_srv,
			     paths_clt, paths_srv);
out:
	return ret;
}
