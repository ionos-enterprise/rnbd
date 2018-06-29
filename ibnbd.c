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
#include "list.h"
#include <assert.h>

#include "ibnbd-sysfs.h"

struct ibnbd_sess s = {
	.sessname = "ps401a-3@st401b-3",
	.active_path_cnt = 2
};

struct ibnbd_sess_dev sd[] = {
	{.mapping_path = "112b5fc0-91f5-4157-8603-777f8e733f1f",
	 .access_mode = IBNBD_RW,
	 .sess = &s,
	 .dev = {.devname = "ibnbd0",
	 	 .devpath = "/dev/ibnbd0",
	 	 .iomode = IBNBD_FILEIO,
		 .rx_sect = 190,
		 .tx_sect = 2342,
	 	 .state = "open"}},

	{.mapping_path = "8f749d51-c7c2-41cc-a55e-4fca8b97de73",
	 .access_mode = IBNBD_RO,
	 .sess = &s,
	 .dev = {.devname = "ibnbd1",
	 	 .devpath = "/dev/ibnbd1",
	 	 .iomode = IBNBD_BLOCKIO,
		 .rx_sect = 190,
		 .tx_sect = 2342,
	 	 .state = "open"}},

	{.mapping_path = "ecf6bfd0-3dae-46a3-9a1b-e61225920185",
	 .access_mode = IBNBD_MIGRATION,
	 .sess = &s,
	 .dev = {.devname = "ibnbd2",
	 	 .devpath = "/dev/ibnbd2",
	 	 .iomode = IBNBD_BLOCKIO,
		 .rx_sect = 190,
		 .tx_sect = 2342,
	 	 .state = "closed"}},
};

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

struct args {
	char pname[65];

	uint64_t size_sect;
	short size_set;
	short sign;

	enum fmt_type fmt;
	short fmt_set;

	unsigned iomode;
	short iomode_set;

	unsigned lstmode;
	short lstmode_set;

	unsigned ibnbdmode;
	short ibnbdmode_set;

	short ro;
	short ro_set;

	struct table_column *clms[CLM_MAX_CNT];
	short clms_set;

	short noterm_set;
	short help_set;
	short verbose_set;

	int unit_id;
	short unit_set;
	char unit[5];
	int prec;
	short prec_set;

	short noheaders_set;
	short nototals_set;
	short force_set;
};

static struct args args;

static int sect_to_byte_unit(char *str, size_t len, uint64_t v, int humanize)
{
	if (humanize)
		if (args.unit_set)
			return i_to_str_unit(v << 9, str, len, args.unit_id,
					     args.prec);

		else
			return i_to_str(v << 9, str, len, args.prec);
	else
		return snprintf(str, len, "%" PRIu64, v << 9);
}

static int size_to_str(char *str, size_t len, enum color *clr, void *v,
		       int humanize)
{
	*clr = CNRM;

	return sect_to_byte_unit(str, len, *(int *)v, humanize);
}

#define CLM_SD(m_name, m_header, m_type, tostr, align, color, m_descr) \
	CLM(ibnbd_sess_dev, m_name, m_header, m_type, tostr, align, color, \
	    m_descr, sizeof(m_header) - 1, 0)

#define _CLM_SD(s_name, m_name, m_header, m_type, tostr, align, color, \
		m_descr) \
	_CLM(ibnbd_sess_dev, s_name, m_name, m_header, m_type, tostr, \
	    align, color, m_descr, sizeof(m_header) - 1, 0)

#define CLM_SDD(m_name, m_header, m_type, tostr, align, color, \
		m_descr) \
	CLM(ibnbd_dev, m_name, m_header, m_type, tostr, align, color, \
	    m_descr, sizeof(m_header) - 1, offsetof(struct ibnbd_sess_dev, dev))

static int name_to_str(char *str, size_t len, enum color *clr, void *v,
		       int humanize)
{
	*clr = CBLD;

	return snprintf(str, len, "%s", (char *)v);
}

static int state_to_str(char *str, size_t len, enum color *clr, void *v,
		        int humanize)
{
	if (!strcmp("open", (char *)v))
		*clr = CGRN;
	else
		*clr = CRED;

	return snprintf(str, len, "%s", (char *)v);
}

CLM_SD(mapping_path, "Mapping Path", FLD_STR, name_to_str, 'l', CNRM,
       "Mapping name of the remote device");

static int sd_access_mode_to_str(char *str, size_t len, enum color *clr,
				  void *v, int humanize)
{
	enum ibnbd_access_mode mode = *(enum ibnbd_access_mode *)v;

	switch (mode) {
	case IBNBD_RO:
		*clr = CGRN;
		return snprintf(str, len, "ro");
	case IBNBD_RW:
		*clr = CNRM;
		return snprintf(str, len, "rw");
	case IBNBD_MIGRATION:
		*clr = CBLU;
		return snprintf(str, len, "migration");
	default:
		*clr = CUND;
		return snprintf(str, len, "%d", mode);
	}
}

static int sdd_iomode_to_str(char *str, size_t len, enum color *clr, void *v,
			     int humanize)
{
	enum ibnbd_iomode mode = *(enum ibnbd_iomode *)v;

	*clr = CNRM;

	switch (mode) {
	case IBNBD_FILEIO:
		return snprintf(str, len, "fileio");
	case IBNBD_BLOCKIO:
		return snprintf(str, len, "blockio");
	default:
		*clr = CUND;
		return snprintf(str, len, "%d", mode);
	}
}

CLM_SD(access_mode, "Access Mode", FLD_STR, sd_access_mode_to_str, 'l', CNRM,
       "Mode of access to the remote device: ro, rw or migration");

CLM_SDD(devname, "Device", FLD_STR, NULL, 'l', CNRM,
	"Device name under /dev/. I.e. ibnbd0");

CLM_SDD(devpath, "Device Path", FLD_STR, NULL, 'l', CNRM,
	"Device path under /dev/. I.e. /dev/ibnbd0");

CLM_SDD(iomode, "IO Mode", FLD_STR, sdd_iomode_to_str, 'l', CNRM,
	"The way target device is accessed on server: fileio/blockio");

CLM_SDD(rx_sect, "RX", FLD_NUM, size_to_str, 'l', CNRM,
	"Amount of data read from the device");

CLM_SDD(tx_sect, "TX", FLD_NUM, size_to_str, 'l', CNRM,
	"Amount of data written to the device");

CLM_SDD(state, "State", FLD_STR, state_to_str, 'l', CNRM,
	"State of the IBNBD device");

static int dev_sessname_to_str(char *str, size_t len, enum color *clr,
			       void *v, int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev, sess);

	return snprintf(str, len, "%s", sd->sess->sessname);
}

static struct table_column clm_ibnbd_sess_dev_sessname =
	_CLM_SD("sessname", sess, "Session", FLD_STR, dev_sessname_to_str, 'l',
		CNRM, "Name of the IBTRS session of the device");

static struct table_column *all_clms_sd[] = {
	&clm_ibnbd_sess_dev_sessname,
	&clm_ibnbd_sess_dev_mapping_path,
	&clm_ibnbd_dev_devname,
	&clm_ibnbd_dev_state,
	&clm_ibnbd_sess_dev_access_mode,
	&clm_ibnbd_dev_iomode,
	&clm_ibnbd_dev_rx_sect,
	&clm_ibnbd_dev_tx_sect,
	NULL
};
#define ALL_CLMS_SD_CNT (ARRSIZE(all_clms_sd) - 1)

static struct table_column *default_clms_sd[] = {
	&clm_ibnbd_sess_dev_sessname,
	&clm_ibnbd_sess_dev_mapping_path,
	&clm_ibnbd_dev_devname,
	&clm_ibnbd_dev_state,
	&clm_ibnbd_sess_dev_access_mode,
	&clm_ibnbd_dev_iomode,
	NULL
};
#define DEFAULT_CLMS_SD_CNT (ARRSIZE(default_clms_sd) - 1)

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
	else
		assert(0);

	args.fmt_set = 1;

	return i + 1;
}

static int parse_iomode(int argc, char **argv, int i, const struct sarg *sarg)
{
	if (!strcasecmp(argv[i], "blockio"))
		args.iomode = IBNBD_BLOCKIO;
	else if (!strcasecmp(argv[i], "fileio"))
		args.iomode = IBNBD_FILEIO;
	else
		assert(0);

	args.iomode_set = 1;

	return i + 1;
}

enum lstmode {
	LST_DEVICES,
	LST_SESSIONS,
	LST_PATHS
};

static int parse_lst(int argc, char **argv, int i, const struct sarg *sarg)
{
	if (!strcasecmp(argv[i], "devices"))
		args.lstmode = LST_DEVICES;
	else if (!strcasecmp(argv[i], "sessions"))
		args.lstmode = LST_SESSIONS;
	else if (!strcasecmp(argv[i], "paths"))
		args.lstmode = LST_PATHS;
	else
		assert(0);

	args.lstmode_set = 1;

	return i + 1;
}

enum ibnbdmode {
	IBNBD_CLIENT,
	IBNBD_SERVER,
	IBNBD_BOTH,
};

static int parse_mode(int argc, char **argv, int i, const struct sarg *sarg)
{
	if (!strcasecmp(argv[i], "client"))
		args.ibnbdmode = IBNBD_CLIENT;
	else if (!strcasecmp(argv[i], "server"))
		args.ibnbdmode = IBNBD_SERVER;
	else if (!strcasecmp(argv[i], "both"))
		args.ibnbdmode = IBNBD_BOTH;
	else
		assert(0);

	args.ibnbdmode_set = 1;

	return i + 1;
}

static int parse_rw(int argc, char **argv, int i, const struct sarg *sarg)
{
	if (!strcasecmp(argv[i], "ro"))
		args.ro = IBNBD_RO;
	else if (!strcasecmp(argv[i], "rw"))
		args.ro = IBNBD_RW;
	else if (!strcasecmp(argv[i], "migration"))
		args.ro = IBNBD_MIGRATION;
	else
		assert(0);

	args.ro_set = 1;

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
	/*clm_set_hdr_unit(&clm_vol_d_usage, sarg->descr);
	clm_set_hdr_unit(&clm_andbd_vol_virt_size, sarg->descr);
	clm_set_hdr_unit(&clm_size, sarg->descr);
	clm_set_hdr_unit(&clm_used, sarg->descr);
	clm_set_hdr_unit(&clm_available, sarg->descr);*/

	args.unit_set = 1;
	return i + 1;
}

static int parse_all(int argc, char **argv, int i, const struct sarg *sarg)
{
	memcpy(&args.clms, &all_clms_sd, ALL_CLMS_SD_CNT * sizeof(all_clms_sd[0]));
	args.clms_set = 1;
	/*memcpy(&args.info_clms, &u_clmns, CLM_DISK_USAGE_CNT * sizeof(u_clmns[0]));
	args.info_clms_set = 1;
	memcpy(&args.usage_clms, &st_usage_clmns,
	       CLM_STORAGE_USAGE_CNT * sizeof(st_usage_clmns[0]));
	args.usage_clms_set = 1;*/

	return i + 1;
}

static int parse_flag(int argc, char **argv, int i, const struct sarg *sarg)
{
	*(short *)sarg->f = 1;

	return i + 1;
}

static struct sarg sargs[] = {
	{"client", "Information for client", parse_mode, NULL},
	{"server", "Information for server", parse_mode, NULL},
	{"bot", "Information for bit", parse_mode, NULL},
	{"devices", "List mapped devices", parse_lst, NULL},
	{"sessions", "List sessions", parse_lst, NULL},
	{"paths", "List paths", parse_lst, NULL},
	{"xml", "Print in XML format", parse_fmt, NULL},
	{"csv", "Print in CSV format", parse_fmt, NULL},
	{"json", "Print in JSON format", parse_fmt, NULL},
	{"ro", "Readonly", parse_rw, NULL},
	{"rw", "Writable", parse_rw, NULL},
	{"migration", "Writable (migration)", parse_rw, NULL},
	{"blockio", "Block IO mode", parse_iomode, NULL},
	{"fileio", "File IO mode", parse_iomode, NULL},
	{"help", "Display help and exit", parse_flag, &args.help_set},
	{"verbose", "Verbose output", parse_flag, &args.verbose_set},
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
	printf("\nAdministration utility for the IBNBD/IBTRS driver.\n");
	printf("\nSubcommands:\n");
	do
		printf("     %-*s%s\n", 20, (*cmds).cmd, (*cmds).short_d);
	while ((*++cmds).cmd);
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

static void help_fields(struct table_column **def, struct table_column **all)
{
	print_opt("{fields}",
		  "Comma separated list of fields to be printed.");
	printf("%sThe list can be prefixed with '+' or '-' to add or remove fields\n", HPRE);
	printf("%sfrom the default selection.\n", HPRE);
	printf("%sDefault: ", HPRE);
	print_clms_list(def);
	printf("\n%s%s\n", HPRE, "Available fields:");
	table_tbl_print_term(HPRE, all, trm);
	printf("\n%sProvide 'all' to print all available fields\n", HPRE);
}

static void help_list(struct cmd *cmd)
{
	cmd_print_usage(cmd, "");

	printf("\nOptions:\n");
	help_fields(default_clms_sd, all_clms_sd);
	print_opt("{mode}", "Information to print: devices|sessions|paths. Default: devices");
	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_sarg_descr("noheaders");
	print_sarg_descr("nototals");
	print_sarg_descr("help");
}

static int clm_cnt(struct table_column **cs)
{
	int i = 0;

	while (cs[i])
		i++;

	return i;
}

static int has_num(struct table_column **cs)
{
	int i = 0;

	while (cs[i]) {
		if (cs[i]->m_type == FLD_NUM)
			return 1;
		i++;
	}

	return 0;
}

static int list_devices_term(struct table_column **cs)
{
	struct ibnbd_sess_dev total = {
		.dev = {
			.rx_sect = 0,
			.tx_sect = 0
		},
		.mapping_path = ""
	};
	struct table_fld *flds;
	int i, cs_cnt, dev_num;

	cs_cnt = clm_cnt(cs);

	dev_num = ARRSIZE(sd);

	if (!has_num(cs))
		args.nototals_set = 1;

	flds = calloc((dev_num + 1) * cs_cnt, sizeof(*flds));
	if (!flds) {
		ERR("not enough memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < dev_num; i++) {
		table_row_stringify(&sd[i], flds + i * cs_cnt, cs, 1, 0);
		total.dev.rx_sect += sd[i].dev.rx_sect;
		total.dev.tx_sect += sd[i].dev.tx_sect;
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

static int list_devices()
{
	int rc;

	switch (args.fmt) {
	case FMT_CSV:
	case FMT_JSON:
	case FMT_XML:
		printf("TODO\n");
		rc = -ENOENT;
		break;
	case FMT_TERM:
	default:
		rc = list_devices_term(args.clms);
		break;
	}

	return rc;
}

static int cmd_list(void)
{
	int rc;

	switch (args.lstmode) {
	case LST_DEVICES:
	default:
		rc = list_devices();
		break;
	case LST_SESSIONS:
	case LST_PATHS:
		printf("TODO\n");
		rc = -ENOENT;
		break;
	}

	return rc;
}

static struct cmd cmds[] = {
	{"list", "List ibnbd block- and transport information",
		 "List ibnbd block- and transport information: "
		 "devices, sessions, paths, etc.", cmd_list, NULL, help_list},
	{"help", "Display help", "Display help message and exit.",
		cmd_help, NULL, NULL},

	{ 0 }
};

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
	unsigned prec;
	char e;

	if (strncmp(str, "prec", 4))
		return -EINVAL;

	if (sscanf(str + 4, "%u%c\n", &prec, &e) != 1)
		return -EINVAL;

	args.prec = prec;
	args.prec_set = 1;

	return 0;
}

static int parse_sd_columns(const char *arg)
{
	int rc;

	rc = table_extend_columns(arg, ",", all_clms_sd, args.clms, CLM_MAX_CNT);
	if (rc)
		return rc;

	return 0;
}

static void init_args(void)
{
	memcpy(&args.clms, &default_clms_sd,
	       DEFAULT_CLMS_SD_CNT * sizeof(all_clms_sd[0]));
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
		/* TODO detect client, server or what */
		args.ibnbdmode = IBNBD_BOTH;
		args.ibnbdmode_set = 1;
	}
}

int main(int argc, char **argv)
{
	int ret = 0, i;
	struct cmd *cmd;
	struct sarg *sarg;

	trm = (isatty(STDOUT_FILENO) == 1);

	init_args();

	snprintf(args.pname, sizeof(args.pname), "%s", argv[0]);

	if (argc < 2) {
		ERR("no command specified\n");
		print_usage(argv[0], cmds);
		ret = -EINVAL;
		goto out;
	}

	i = 1;
	cmd = find_cmd(argv[i], cmds);
	if (!cmd) {
		printf("'%s' is not a valid command. Try '%s%s%s %s%s%s'\n",
		       argv[i], CLR(trm, CBLD, argv[0]),
		       CLR(trm, CBLD, "help"));
		handle_unknown_cmd(argv[i], cmds);
		ret = -EINVAL;
		goto out;
	}

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
			if (!parse_sd_columns(argv[i]) ||
			    !parse_precision(argv[i])) {
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

	default_args();

	ret = 0;
	if (args.help_set && cmd->help)
		cmd->help(cmd);
	else if (cmd->func)
		ret = cmd->func();

out:
	return ret;
}
