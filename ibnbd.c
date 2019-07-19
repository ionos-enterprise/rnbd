#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>	/* for isatty() */
#include <assert.h>
#include <dirent.h>

#include "levenshtein.h"
#include "table.h"
#include "misc.h"
#include "list.h"

#include "ibnbd-sysfs.h"

struct ibnbd_sess s = {
	.sessname = "ps401a-3@st401b-3",
	.mp = "round-robin",
	.mp_short = "RR",
	.act_path_cnt = 2,
	.path_cnt = 2,
	.tx_bytes = 1023,
	.rx_bytes = 377000,
	.inflights = 100500,
	.reconnects = 5,
	.path_uu = "UU",
	.paths = {
		{.sess = &s,
		 .pathname = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d5@"
			     "gid:fe80:0000:0000:0000:0002:c903:0010:c0f5",
		 .cltaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d5",
		 .srvaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0f5",
		 .hca_name = "mlx4_0",
		 .hca_port = 1,
		 .state = "connected",
		 .tx_bytes = 1023,
		 .rx_bytes = 377000,
		 .inflights = 100500,
		 .reconnects = 2
		},
		{.sess = &s,
		 .pathname = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d6@"
			     "gid:fe80:0000:0000:0000:0002:c903:0010:c0f6",
		 .cltaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d6",
		 .srvaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0f6",
		 .hca_name = "mlx4_0",
		 .hca_port = 2,
		 .state = "connected",
		 .tx_bytes = 0,
		 .rx_bytes = 0,
		 .inflights = 0,
		 .reconnects = 3
		}
	}
};

struct ibnbd_sess s1 = {
	.sessname = "ps401a-3@st401b-4",
	.mp = "min-inflight",
	.mp_short = "MI",
	.act_path_cnt = 1,
	.path_cnt = 2,
	.tx_bytes = 1023,
	.rx_bytes = 377000,
	.inflights = 100500,
	.reconnects = 5,
	.path_uu = "_U",
	.paths = {
		{.sess = &s1,
		 .pathname = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d5@"
			     "gid:fe80:0000:0000:0000:0002:c903:0010:c0f5",
		 .cltaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d5",
		 .srvaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0f5",
		 .hca_name = "mlx4_0",
		 .hca_port = 1,
		 .state = "disconnected",
		 .tx_bytes = 0,
		 .rx_bytes = 377000,
		 .inflights = 0,
		 .reconnects = 1
		},
		{.sess = &s1,
		 .pathname = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d6@"
			     "gid:fe80:0000:0000:0000:0002:c903:0010:c0f6",
		 .cltaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d6",
		 .srvaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0f6",
		 .hca_name = "mlx4_0",
		 .hca_port = 2,
		 .state = "connected",
		 .tx_bytes = 1023,
		 .rx_bytes = 0,
		 .inflights = 100500,
		 .reconnects = 4
		}
	}
};

struct ibnbd_sess *sessions[] = {
	&s,
	&s1,
	NULL
};

struct ibnbd_dev d[] = {
	{.devname = "ibnbd0",
	 .devpath = "/dev/ibnbd0",
	 .iomode = IBNBD_FILEIO,
	 .rx_sect = 190,
	 .tx_sect = 2342,
	 .state = "open"
	},
	{.devname = "ibnbd1",
	 .devpath = "/dev/ibnbd1",
	 .iomode = IBNBD_BLOCKIO,
	 .rx_sect = 190,
	 .tx_sect = 2342,
	 .state = "open"
	},
	{.devname = "ibnbd2",
	 .devpath = "/dev/ibnbd2",
	 .iomode = IBNBD_BLOCKIO,
	 .rx_sect = 190,
	 .tx_sect = 2342,
	 .state = "closed"
	},
	{.devname = "ram0",
	 .devpath = "/dev/ram0",
	 .iomode = IBNBD_BLOCKIO,
	 .rx_sect = 190,
	 .tx_sect = 2342,
	 .state = "closed"
	},
};

struct ibnbd_sess_dev sd[] = {
	{.mapping_path = "112b5fc0-91f5-4157-8603-777f8e733f1f",
	 .access_mode = IBNBD_RW,
	 .sess = &s,
	 .dev = &d[0]
	},
	{.mapping_path = "8f749d51-c7c2-41cc-a55e-4fca8b97de73",
	 .access_mode = IBNBD_RO,
	 .sess = &s,
	 .dev = &d[1]
	},
	{.mapping_path = "ecf6bfd0-3dae-46a3-9a1b-e61225920185",
	 .access_mode = IBNBD_MIGRATION,
	 .sess = &s,
	 .dev = &d[2]
	},
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
	char name[NAME_MAX];

	uint64_t size_sect;
	short size_set;
	short sign;

	enum fmt_type fmt;
	short fmt_set;

	unsigned iomode;
	short iomode_set;

	unsigned lstmode;
	short lstmode_set;

	unsigned showmode;
	short showmode_set;

	unsigned ibnbdmode;
	short ibnbdmode_set;

	short ro;
	short ro_set;

	struct table_column *clms_devices_clt[CLM_MAX_CNT];
	struct table_column *clms_devices_srv[CLM_MAX_CNT];

	struct table_column *clms_sessions_clt[CLM_MAX_CNT];
	struct table_column *clms_sessions_srv[CLM_MAX_CNT];

	struct table_column *clms_paths_clt[CLM_MAX_CNT];
	struct table_column *clms_paths_srv[CLM_MAX_CNT];

	short tree_set;
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

#define CLM_SD(m_name, m_header, m_type, tostr, align, h_clr, c_clr, m_descr) \
	CLM(ibnbd_sess_dev, m_name, m_header, m_type, tostr, align, h_clr,\
	    c_clr, m_descr, sizeof(m_header) - 1, 0)

#define _CLM_SD(s_name, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
		m_descr) \
	_CLM(ibnbd_sess_dev, s_name, m_name, m_header, m_type, tostr, \
	    align, h_clr, c_clr, m_descr, sizeof(m_header) - 1, 0)

static int sd_state_to_str(char *str, size_t len, enum color *clr, void *v,
		           int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev, sess);

	if (!strcmp("open", sd->dev->state))
		*clr = CGRN;
	else
		*clr = CRED;

	return snprintf(str, len, "%s", sd->dev->state);
}

CLM_SD(mapping_path, "Mapping Path", FLD_STR, NULL, 'l', CNRM, CBLD,
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
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev, sess);
	enum ibnbd_iomode mode = sd->dev->iomode;

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

CLM_SD(access_mode, "Access Mode", FLD_STR, sd_access_mode_to_str, 'l', CNRM, CNRM,
       "RW mode of the device: ro, rw or migration");

static int sd_devname_to_str(char *str, size_t len, enum color *clr,
			     void *v, int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev, sess);

	return snprintf(str, len, "%s", sd->dev->devname);
}

static struct table_column clm_ibnbd_dev_devname =
	_CLM_SD("devname", sess, "Device", FLD_STR, sd_devname_to_str, 'l',
		CNRM, CNRM, "Device name under /dev/. I.e. ibnbd0");

static int sd_devpath_to_str(char *str, size_t len, enum color *clr,
			     void *v, int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev, sess);

	return snprintf(str, len, "%s", sd->dev->devpath);
}

static struct table_column clm_ibnbd_dev_devpath =
	_CLM_SD("devpath", sess, "Device path", FLD_STR, sd_devpath_to_str, 'l',
		CNRM, CNRM, "Device path under /dev/. I.e. /dev/ibnbd0");

static struct table_column clm_ibnbd_dev_iomode =
	_CLM_SD("iomode", sess, "IO mode", FLD_STR, sdd_iomode_to_str, 'l',
		CNRM, CNRM, "IO submission mode of the target device: file/block");

static int sd_rx_to_str(char *str, size_t len, enum color *clr, void *v,
		       int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev, sess);

	*clr = CNRM;

	return sect_to_byte_unit(str, len, sd->dev->rx_sect, humanize);
}

static struct table_column clm_ibnbd_dev_rx_sect =
	_CLM_SD("rx_sect", sess, "RX", FLD_NUM, sd_rx_to_str, 'l', CNRM, CNRM,
	"Amount of data read from the device");

static int sd_tx_to_str(char *str, size_t len, enum color *clr, void *v,
		       int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev, sess);

	*clr = CNRM;

	return sect_to_byte_unit(str, len, sd->dev->tx_sect, humanize);
}

static struct table_column clm_ibnbd_dev_tx_sect =
	_CLM_SD("tx_sect", sess, "TX", FLD_NUM, sd_tx_to_str, 'l', CNRM, CNRM,
	"Amount of data written to the device");


static struct table_column clm_ibnbd_dev_state =
	_CLM_SD("state", sess, "State", FLD_STR, sd_state_to_str, 'l', CNRM, CNRM,
	"State of the IBNBD device. (client only)");

static int dev_sessname_to_str(char *str, size_t len, enum color *clr,
			       void *v, int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev, sess);

	return snprintf(str, len, "%s", sd->sess->sessname);
}

static struct table_column clm_ibnbd_sess_dev_sessname =
	_CLM_SD("sessname", sess, "Session", FLD_STR, dev_sessname_to_str, 'l',
		CNRM, CNRM, "Name of the IBTRS session of the device");

static struct table_column *all_clms_devices[] = {
	&clm_ibnbd_sess_dev_sessname,
	&clm_ibnbd_sess_dev_mapping_path,
	&clm_ibnbd_dev_devname,
	&clm_ibnbd_dev_devpath,
	&clm_ibnbd_dev_state,
	&clm_ibnbd_sess_dev_access_mode,
	&clm_ibnbd_dev_iomode,
	&clm_ibnbd_dev_rx_sect,
	&clm_ibnbd_dev_tx_sect,
	NULL
};
#define ALL_CLMS_DEVICES_CNT (ARRSIZE(all_clms_devices) - 1)

static struct table_column *def_clms_devices_clt[] = {
	&clm_ibnbd_sess_dev_sessname,
	&clm_ibnbd_sess_dev_mapping_path,
	&clm_ibnbd_dev_devname,
	&clm_ibnbd_dev_state,
	&clm_ibnbd_sess_dev_access_mode,
	&clm_ibnbd_dev_iomode,
	NULL
};
#define DEF_CLMS_DEVICES_CLT_CNT (ARRSIZE(def_clms_devices_clt) - 1)

static struct table_column *def_clms_devices_srv[] = {
	&clm_ibnbd_sess_dev_sessname,
	&clm_ibnbd_sess_dev_mapping_path,
	&clm_ibnbd_dev_devname,
	&clm_ibnbd_sess_dev_access_mode,
	&clm_ibnbd_dev_iomode,
	NULL
};
#define DEF_CLMS_DEVICES_SRV_CNT (ARRSIZE(def_clms_devices_srv) - 1)

#define CLM_S(m_name, m_header, m_type, tostr, align, h_clr, c_clr, m_descr) \
	CLM(ibnbd_sess, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
	    m_descr, sizeof(m_header) - 1, 0)

#define _CLM_S(s_name, m_name, m_header, m_type, tostr, align, h_clr, c_clr, m_descr) \
	_CLM(ibnbd_sess, s_name, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
	    m_descr, sizeof(m_header) - 1, 0)

CLM_S(sessname, "Session name", FLD_STR, NULL, 'l', CNRM, CBLD,
       "Name of the session");
CLM_S(mp_short, "MP", FLD_STR, NULL, 'l', CNRM, CNRM,
       "Multipath policy (short)");
CLM_S(mp, "MP Policy", FLD_STR, NULL, 'l', CNRM, CNRM,
       "Multipath policy");
CLM_S(path_cnt, "Path cnt", FLD_NUM, NULL, 'r', CNRM, CNRM,
       "Number of paths");
CLM_S(act_path_cnt, "Act path cnt", FLD_NUM, NULL, 'r', CNRM, CNRM,
       "Number of active paths");
CLM_S(rx_bytes, "RX", FLD_NUM, size_to_str, 'r', CNRM, CNRM, "Bytes received");
CLM_S(tx_bytes, "TX", FLD_NUM, size_to_str, 'r', CNRM, CNRM, "Bytes send");
CLM_S(inflights, "Inflights", FLD_NUM, NULL, 'r', CNRM, CNRM, "Inflights");
CLM_S(reconnects, "Reconnects", FLD_NUM, NULL, 'r', CNRM, CNRM, "Reconnects");
CLM_S(path_uu, "PS", FLD_STR, NULL, 'l', CNRM, CNRM,
      "Up (U) or down (_) state of every path");

static int act_path_cnt_to_state(char *str, size_t len, enum color *clr,
				    void *v, int humanize)
{
	if (*(int *)v) {
		*clr = CGRN;
		return snprintf(str, len, "connected");
	} else {
		*clr = CRED;
		return snprintf(str, len, "disconnected");
	}
}

static struct table_column clm_ibnbd_sess_state =
	_CLM_S("state", act_path_cnt, "State", FLD_STR,
		act_path_cnt_to_state, 'l', CNRM, CNRM,
		"State of the session.");

static struct table_column *all_clms_sessions[] = {
	&clm_ibnbd_sess_sessname,
	&clm_ibnbd_sess_path_cnt,
	&clm_ibnbd_sess_act_path_cnt,
	&clm_ibnbd_sess_state,
	&clm_ibnbd_sess_path_uu,
	&clm_ibnbd_sess_mp,
	&clm_ibnbd_sess_mp_short,
	&clm_ibnbd_sess_rx_bytes,
	&clm_ibnbd_sess_tx_bytes,
	&clm_ibnbd_sess_inflights,
	&clm_ibnbd_sess_reconnects,
	NULL
};
#define ALL_CLMS_SESSIONS_CNT (ARRSIZE(all_clms_sessions) - 1)

static struct table_column *def_clms_sessions_clt[] = {
	&clm_ibnbd_sess_sessname,
	&clm_ibnbd_sess_state,
	&clm_ibnbd_sess_path_uu,
	&clm_ibnbd_sess_mp,
	&clm_ibnbd_sess_tx_bytes,
	&clm_ibnbd_sess_rx_bytes,
	&clm_ibnbd_sess_inflights,
	&clm_ibnbd_sess_reconnects,
	NULL
};
#define DEF_CLMS_SESSIONS_CLT_CNT (ARRSIZE(def_clms_sessions_clt) - 1)

static struct table_column *def_clms_sessions_srv[] = {
	&clm_ibnbd_sess_sessname,
	&clm_ibnbd_sess_path_uu,
	&clm_ibnbd_sess_tx_bytes,
	&clm_ibnbd_sess_rx_bytes,
	&clm_ibnbd_sess_inflights,
	&clm_ibnbd_sess_reconnects,
	NULL
};
#define DEF_CLMS_SESSIONS_SRV_CNT (ARRSIZE(def_clms_sessions_srv) - 1)

#define CLM_P(m_name, m_header, m_type, tostr, align, h_clr, c_clr, m_descr) \
	CLM(ibnbd_path, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
	    m_descr, sizeof(m_header) - 1, 0)

static int ibnbd_path_state_to_str(char *str, size_t len, enum color *clr,
				   void *v, int humanize)
{
	struct ibnbd_path *p = container_of(v, struct ibnbd_path, state);

	if (!strcmp(p->state, "connected"))
		*clr = CGRN;
	else
		*clr = CRED;

	return snprintf(str, len, "%s", p->state);
}

CLM_P(state, "State", FLD_STR, ibnbd_path_state_to_str, 'l', CNRM, CBLD,
       "Name of the path");
CLM_P(pathname, "Path name", FLD_STR, NULL, 'l', CNRM, CNRM,
      "Path name");
CLM_P(cltaddr, "Clt Addr", FLD_STR, NULL, 'l', CNRM, CNRM,
      "Client address");
CLM_P(srvaddr, "Srv Addr", FLD_STR, NULL, 'l', CNRM, CNRM,
      "Server address");
CLM_P(hca_name, "HCA", FLD_STR, NULL, 'l', CNRM, CNRM, "HCA name");
CLM_P(hca_port, "Port", FLD_VAL, NULL, 'r', CNRM, CNRM, "HCA port");
CLM_P(rx_bytes, "RX", FLD_NUM, size_to_str, 'r', CNRM, CNRM, "Bytes received");
CLM_P(tx_bytes, "TX", FLD_NUM, size_to_str, 'r', CNRM, CNRM, "Bytes send");
CLM_P(inflights, "Inflights", FLD_NUM, NULL, 'r', CNRM, CNRM, "Inflights");
CLM_P(reconnects, "Reconnects", FLD_NUM, NULL, 'r', CNRM, CNRM, "Reconnects");

static int path_to_sessname(char *str, size_t len, enum color *clr,
			    void *v, int humanize)
{
	struct ibnbd_path *p = container_of(v, struct ibnbd_path, sess);

	*clr = CNRM;

	if (p->sess)
		return snprintf(str, len, "%s", p->sess->sessname);
	else
		return snprintf(str, len, "%s", "");
}

#define _CLM_P(s_name, m_name, m_header, m_type, tostr, align, h_clr, c_clr, m_descr) \
	_CLM(ibnbd_path, s_name, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
	    m_descr, sizeof(m_header) - 1, 0)

static struct table_column clm_ibnbd_path_sessname =
	_CLM_P("sessname", sess, "Sessname", FLD_STR, path_to_sessname, 'l',
	      CNRM, CNRM, "Name of the session.");

static struct table_column *all_clms_paths[] = {
	&clm_ibnbd_path_sessname,
	&clm_ibnbd_path_pathname,
	&clm_ibnbd_path_cltaddr,
	&clm_ibnbd_path_srvaddr,
	&clm_ibnbd_path_hca_name,
	&clm_ibnbd_path_hca_port,
	&clm_ibnbd_path_state,
	&clm_ibnbd_path_rx_bytes,
	&clm_ibnbd_path_tx_bytes,
	&clm_ibnbd_path_inflights,
	&clm_ibnbd_path_reconnects,
	NULL
};
#define ALL_CLMS_PATHS_CNT (ARRSIZE(all_clms_paths) - 1)

static struct table_column *def_clms_paths_clt[] = {
	&clm_ibnbd_path_sessname,
	&clm_ibnbd_path_hca_name,
	&clm_ibnbd_path_hca_port,
	&clm_ibnbd_path_srvaddr,
	&clm_ibnbd_path_state,
	&clm_ibnbd_path_tx_bytes,
	&clm_ibnbd_path_rx_bytes,
	&clm_ibnbd_path_inflights,
	&clm_ibnbd_path_reconnects,
	NULL
};
#define DEF_CLMS_PATHS_CLT_CNT (ARRSIZE(def_clms_paths_clt) - 1)

static struct table_column *def_clms_paths_srv[] = {
	&clm_ibnbd_path_sessname,
	&clm_ibnbd_path_hca_name,
	&clm_ibnbd_path_hca_port,
	&clm_ibnbd_path_cltaddr,
	&clm_ibnbd_path_tx_bytes,
	&clm_ibnbd_path_rx_bytes,
	&clm_ibnbd_path_inflights,
	NULL
};
#define DEF_CLMS_PATHS_SRV_CNT (ARRSIZE(def_clms_paths_srv) - 1)

static struct table_column *clms_paths_sess[] = {
	&clm_ibnbd_path_pathname,
	&clm_ibnbd_path_hca_name,
	&clm_ibnbd_path_hca_port,
	&clm_ibnbd_path_state,
	NULL
};

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
		assert(0);

	args.lstmode_set = 1;
	args.showmode_set = 1;

	return i + 1;
}

enum ibnbdmode {
	IBNBD_NONE = 0,
	IBNBD_CLIENT = 1,
	IBNBD_SERVER = 1 << 1,
	IBNBD_BOTH = IBNBD_CLIENT | IBNBD_SERVER,
};

static int parse_mode(int argc, char **argv, int i, const struct sarg *sarg)
{
	if (!strcasecmp(argv[i], "client") || !strcasecmp(argv[i], "clt"))
		args.ibnbdmode = IBNBD_CLIENT;
	else if (!strcasecmp(argv[i], "server") || !strcasecmp(argv[i], "srv"))
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
	clm_set_hdr_unit(&clm_ibnbd_sess_rx_bytes, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_sess_tx_bytes, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_path_rx_bytes, sarg->descr);
	clm_set_hdr_unit(&clm_ibnbd_path_tx_bytes, sarg->descr);

	args.unit_set = 1;
	return i + 1;
}

static int parse_all(int argc, char **argv, int i, const struct sarg *sarg)
{
	memcpy(&args.clms_devices_clt, &all_clms_devices,
	       ALL_CLMS_DEVICES_CNT * sizeof(all_clms_devices[0]));
	memcpy(&args.clms_devices_srv, &all_clms_devices,
	       ALL_CLMS_DEVICES_CNT * sizeof(all_clms_devices[0]));
	memcpy(&args.clms_sessions_clt, &all_clms_sessions,
	       ALL_CLMS_SESSIONS_CNT * sizeof(all_clms_sessions[0]));
	memcpy(&args.clms_sessions_srv, &all_clms_sessions,
	       ALL_CLMS_SESSIONS_CNT * sizeof(all_clms_sessions[0]));
	memcpy(&args.clms_paths_clt, &all_clms_paths,
	       ALL_CLMS_PATHS_CNT * sizeof(all_clms_paths[0]));
	memcpy(&args.clms_paths_srv, &all_clms_paths,
	       ALL_CLMS_PATHS_CNT * sizeof(all_clms_paths[0]));


	return i + 1;
}

static int parse_flag(int argc, char **argv, int i, const struct sarg *sarg)
{
	*(short *)sarg->f = 1;

	return i + 1;
}

static struct sarg sargs[] = {
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
	{"tree", "Display paths for each sessions", parse_flag, &args.tree_set},
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

	printf("%s%s%s%s\n\n", HPRE, CLR(trm, CUND, "List of devices:"));
	print_fields(def_clms_devices_clt, def_clms_devices_srv,
		     all_clms_devices);

	printf("%s%s%s%s\n\n", HPRE, CLR(trm, CUND, "List of sessions:"));
	print_fields(def_clms_sessions_clt, def_clms_sessions_srv,
		     all_clms_sessions);

	printf("%s%s%s%s\n\n", HPRE, CLR(trm, CUND, "List of paths:"));
	print_fields(def_clms_paths_clt, def_clms_paths_srv, all_clms_paths);

	printf("\n%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_sarg_descr("tree");
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

static int list_devices_term(struct ibnbd_sess_dev *sd, int dev_num,
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
	int i, cs_cnt;

	cs_cnt = clm_cnt(cs);

	if (!has_num(cs))
		args.nototals_set = 1;

	flds = calloc((dev_num + 1) * cs_cnt, sizeof(*flds));
	if (!flds) {
		ERR("not enough memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < dev_num; i++) {
		table_row_stringify(&sd[i], flds + i * cs_cnt, cs, 1, 0);
		total.dev->rx_sect += sd[i].dev->rx_sect;
		total.dev->tx_sect += sd[i].dev->tx_sect;
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
	struct table_column **cs;
	int i, rc = 0, dev_num;

	switch (args.ibnbdmode) {
	case IBNBD_CLIENT:
	case IBNBD_BOTH:
		cs = args.clms_devices_clt;
		break;
	case IBNBD_SERVER:
		cs = args.clms_devices_srv;
		break;
	default:
		assert(0);
	}

	dev_num = ARRSIZE(sd);

	switch (args.fmt) {
	case FMT_CSV:
		if (!args.noheaders_set)
			table_header_print_csv(cs);

		for (i = 0; i < dev_num; i++) {
			table_row_print(&sd[i], FMT_CSV, "", cs, 0, 0, 0);
		}
		break;
	case FMT_JSON:
		printf("{ \"devices\": [\n");

		for (i = 0; i < dev_num; i++) {
			if (i)
				printf(",\n");
			table_row_print(&sd[i], FMT_JSON, "\t", cs, 0, 0, 0);
		}

		printf("\n] }\n");
		break;
	case FMT_XML:
		printf("<devices>\n");

		for (i = 0; i < dev_num; i++) {
			printf("\t<device>\n");
			table_row_print(&sd[i], FMT_XML, "\t\t", cs, 0, 0, 0);
			printf("\t</device>\n");
		}

		printf("</devices>\n");

		break;
	case FMT_TERM:
	default:
		rc = list_devices_term(sd, dev_num, cs);
		break;
	}

	return rc;
}

static int list_paths_term(struct ibnbd_sess **sessions, int sess_num,
			   struct table_column **cs, int tree)
{
	struct ibnbd_path total = {
		.rx_bytes = 0,
		.tx_bytes = 0,
		.inflights = 0,
		.reconnects = 0
	};

	struct table_fld *flds;
	int i, j, cs_cnt, path_cnt = 0, fld_cnt = 0;

	cs_cnt = clm_cnt(cs);

	if (!has_num(cs))
		args.nototals_set = 1;

	for (i = 0; i < sess_num; i++)
		path_cnt += sessions[i]->path_cnt;

	flds = calloc((path_cnt + 1) * cs_cnt, sizeof(*flds));
	if (!flds) {
		ERR("not enough memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < sess_num; i++) {
		for (j = 0; j < sessions[i]->path_cnt; j++) {
			table_row_stringify(&sessions[i]->paths[j],
					    flds + fld_cnt, cs, 1, 0);

			fld_cnt += cs_cnt;

			total.rx_bytes += sessions[i]->paths[j].rx_bytes;
			total.tx_bytes += sessions[i]->paths[j].tx_bytes;
			total.inflights += sessions[i]->paths[j].inflights;
			total.reconnects += sessions[i]->paths[j].reconnects;
		}
	}

	if (!args.nototals_set) {
		table_row_stringify(&total, flds + fld_cnt, cs, 1, 0);
	}

	if (!args.noheaders_set && !tree)
		table_header_print_term("", cs, trm, 'a');

	fld_cnt = 0;
	for (i = 0; i < sess_num; i++) {
		for (j = 0; j < sessions[i]->path_cnt; j++) {
			table_flds_print_term(!tree ? "" :
					      j < sessions[i]->path_cnt - 1 ?
					      "├─ " : "└─ ", flds + fld_cnt,
					      cs, trm, 0);
			fld_cnt += cs_cnt;
		}
	}

	if (!args.nototals_set) {
		table_row_print_line("", cs, trm, 1, 0);
		table_flds_del_not_num(flds + fld_cnt, cs);
		table_flds_print_term("", flds + fld_cnt, cs, trm, 0);
	}

	free(flds);

	return 0;
}

static int list_sessions_term(struct ibnbd_sess **sessions, int sess_num,
			      struct table_column **cs, int tree)
{
	struct ibnbd_sess total = {
		.act_path_cnt = 0,
		.path_cnt = 0,
		.rx_bytes = 0,
		.tx_bytes = 0,
		.inflights = 0,
		.reconnects = 0
	};

	struct table_fld *flds;
	int i, cs_cnt;
	cs_cnt = clm_cnt(cs);

	if (!has_num(cs))
		args.nototals_set = 1;

	flds = calloc((sess_num + 1) * cs_cnt, sizeof(*flds));
	if (!flds) {
		ERR("not enough memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < sess_num; i++) {
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

	for (i = 0; i < sess_num; i++) {
		table_flds_print_term("", flds + i * cs_cnt,
				      cs, trm, 0);
		if (tree)
			list_paths_term(&sessions[i], 1, clms_paths_sess, 1);
	}

	if (!args.nototals_set) {
		table_row_print_line("", cs, trm, 1, 0);
		table_flds_del_not_num(flds + sess_num * cs_cnt, cs);
		table_flds_print_term("", flds + sess_num * cs_cnt,
				      cs, trm, 0);
	}

	free(flds);

	return 0;
}

static int list_sessions()
{
	struct table_column **cs;
	int i, rc = 0, sess_num;

	switch (args.ibnbdmode) {
	case IBNBD_CLIENT:
	case IBNBD_BOTH:
		cs = args.clms_sessions_clt;
		break;
	case IBNBD_SERVER:
		cs = args.clms_sessions_srv;
		break;
	default:
		assert(0);
	}

	sess_num = ARRSIZE(sessions) - 1;

	switch (args.fmt) {
	case FMT_CSV:
		if (!args.noheaders_set)
			table_header_print_csv(cs);

		for (i = 0; i < sess_num; i++) {
			table_row_print(sessions[i], FMT_CSV, "", cs, 0, 0, 0);
		}
		break;
	case FMT_JSON:
		printf("{ \"sessions\": [\n");

		for (i = 0; i < sess_num; i++) {
			if (i)
				printf(",\n");
			table_row_print(sessions[i], FMT_JSON, "\t", cs, 0, 0, 0);
		}

		printf("\n] }\n");
		break;
	case FMT_XML:
		printf("<sessions>\n");

		for (i = 0; i < sess_num; i++) {
			printf("\t<session>\n");
			table_row_print(sessions[i], FMT_XML, "\t\t", cs, 0, 0, 0);
			printf("\t</session>\n");
		}

		printf("</sessions>\n");

		break;
	case FMT_TERM:
	default:
		rc = list_sessions_term(sessions, sess_num, cs, args.tree_set);
		break;
	}

	return rc;
}

static int list_paths()
{
	struct table_column **cs;
	int i, j, rc = 0, sess_num;

	switch (args.ibnbdmode) {
	case IBNBD_CLIENT:
	case IBNBD_BOTH:
		cs = args.clms_paths_clt;
		break;
	case IBNBD_SERVER:
		cs = args.clms_paths_srv;
		break;
	default:
		assert(0);
	}

	sess_num = ARRSIZE(sessions) - 1;

	switch (args.fmt) {
	case FMT_CSV:
		if (!args.noheaders_set)
			table_header_print_csv(cs);

		for (i = 0; i < sess_num; i++)
			for (j = 0; j < sessions[i]->path_cnt; j++)
				table_row_print(&sessions[i]->paths[j], FMT_CSV,
						"", cs, 0, 0, 0);

		break;
	case FMT_JSON:
		printf("{ \"paths\": [\n");

		for (i = 0; i < sess_num; i++) {
			for (j = 0; j < sessions[i]->path_cnt; j++) {
				if (i)
					printf(",\n");
				table_row_print(&sessions[i]->paths[j], FMT_JSON,
						"\t", cs, 0, 0, 0);
			}
		}

		printf("\n] }\n");
		break;
	case FMT_XML:
		printf("<paths>\n");

		for (i = 0; i < sess_num; i++) {
			for (j = 0; j < sessions[i]->path_cnt; j++) {
				printf("\t<path>\n");
				table_row_print(&sessions[i]->paths[j], FMT_XML,
						"\t\t", cs, 0, 0, 0);
				printf("\t</path>\n");
			}
		}

		printf("</paths>\n");

		break;
	case FMT_TERM:
	default:
		rc = list_paths_term(sessions, sess_num, cs, 0);
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
		rc = list_sessions();
		break;
	case LST_PATHS:
		rc = list_paths();
		break;
	}

	return rc;
}

/*
 * Find an ibnbd device by device name, path or mapping path
 * TODO
 */
static struct ibnbd_sess_dev *find_device(char *name)
{
	int i;

	for (i = 0; i < ARRSIZE(sd); i++)
		if (!strcmp(sd[i].mapping_path, name) ||
		    !strcmp(sd[i].dev->devname, name) ||
		    !strcmp(sd[i].dev->devpath, name))
			return &sd[i];

	return NULL;
}

static int show_device(char *devname)
{
	struct table_fld flds[CLM_MAX_CNT];
	struct ibnbd_sess_dev *dev;
	struct table_column **cs;

	dev = find_device(devname);

	if (!dev) {
		ERR("Device %s not found\n", devname);
		return -ENOENT;
	}

	switch (args.ibnbdmode) {
	case IBNBD_CLIENT:
	case IBNBD_BOTH:
		cs = args.clms_devices_clt;
		break;
	case IBNBD_SERVER:
		cs = args.clms_devices_srv;
		break;
	default:
		assert(0);
	}

	switch (args.fmt) {
	case FMT_CSV:
		if (!args.noheaders_set)
			table_header_print_csv(cs);
		table_row_print(dev, FMT_CSV, "", cs, 0, 0, 0);
		break;
	case FMT_JSON:
		table_row_print(dev, FMT_JSON, "", cs, 0, 0, 0);
		printf("\n");
		break;
	case FMT_XML:
		table_row_print(dev, FMT_XML, "", cs, 0, 0, 0);
		break;
	case FMT_TERM:
	default:
		table_row_stringify(dev, flds, cs, 1, 0);
		table_entry_print_term("", flds, cs,
				       table_get_max_h_width(cs), trm);
		break;
	}

	return 0;
}

/*
 * Find a session by name or hostname...
 * check if unique...
 * FIXME
 */
static struct ibnbd_sess *find_session(char *name)
{
	struct ibnbd_sess *s;
	int i = 0;
	char *at;

	while ((s = sessions[i++])) {
		if (!strcmp(name, s->sessname))
			return s;

		at = strchr(s->sessname, '@');
		if (at) {
			if (!strcmp(name, at + 1))
				return s;
		}
	}

	return NULL;
}

/*
 * Find a path by sessname:port,sessname:dstaddr,sessname:hca_name:port,
 * sesname:hca
 * TODO
 */
static struct ibnbd_path *find_path(char *name)
{
	struct ibnbd_sess *s;
	int i = 0, j, port;
	char *at;

	at = strrchr(name, ':');
	if (!at)
		return NULL;

	if (sscanf(at + 1, "%d\n", &port) != 1)
		return NULL;

	while ((s = sessions[i++])) {
		if (!strncmp(name, s->sessname, at - name))
			for (j = 0; j < s->path_cnt; j++)
				if (s->paths[j].hca_port == port)
					return &s->paths[j];
	}

	return NULL;
}

static int get_showmode(char *name, enum showmode *mode)
{
	if (find_device(name))
		*mode = SHOW_DEVICE;
	else if (find_session(name))
		*mode = SHOW_SESSION;
	else if (find_path(name))
		*mode = SHOW_PATH;
	else
		return -ENOENT;

	return 0;
}

static int show_path(char *pathname)
{
	struct table_fld flds[CLM_MAX_CNT];
	struct ibnbd_path *path;
	struct table_column **cs;

	path = find_path(pathname);

	if (!path) {
		ERR("Path %s not found\n", pathname);
		return -ENOENT;
	}

	switch (args.ibnbdmode) {
	case IBNBD_CLIENT:
	case IBNBD_BOTH:
		cs = args.clms_paths_clt;
		break;
	case IBNBD_SERVER:
		cs = args.clms_paths_srv;
		break;
	default:
		assert(0);
	}

	switch (args.fmt) {
	case FMT_CSV:
		if (!args.noheaders_set)
			table_header_print_csv(cs);
		table_row_print(path, FMT_CSV, "", cs, 0, 0, 0);
		break;
	case FMT_JSON:
		table_row_print(path, FMT_JSON, "", cs, 0, 0, 0);
		printf("\n");
		break;
	case FMT_XML:
		table_row_print(path, FMT_XML, "", cs, 0, 0, 0);
		break;
	case FMT_TERM:
	default:
		table_row_stringify(path, flds, cs, 1, 0);
		table_entry_print_term("", flds, cs,
				       table_get_max_h_width(cs), trm);
		break;
	}


	return 0;
}

static int show_session(char *sessname)
{
	struct table_fld flds[CLM_MAX_CNT];
	struct ibnbd_sess *sess;
	struct table_column **cs;

	sess = find_session(sessname);

	if (!sess) {
		ERR("Sessions %s not found\n", sessname);
		return -ENOENT;
	}

	switch (args.ibnbdmode) {
	case IBNBD_CLIENT:
	case IBNBD_BOTH:
		cs = args.clms_sessions_clt;
		break;
	case IBNBD_SERVER:
		cs = args.clms_sessions_srv;
		break;
	default:
		assert(0);
	}

	switch (args.fmt) {
	case FMT_CSV:
		if (!args.noheaders_set)
			table_header_print_csv(cs);
		table_row_print(sess, FMT_CSV, "", cs, 0, 0, 0);
		break;
	case FMT_JSON:
		table_row_print(sess, FMT_JSON, "", cs, 0, 0, 0);
		printf("\n");
		break;
	case FMT_XML:
		table_row_print(sess, FMT_XML, "", cs, 0, 0, 0);
		break;
	case FMT_TERM:
	default:
		table_row_stringify(sess, flds, cs, 1, 0);
		table_entry_print_term("", flds, cs,
				       table_get_max_h_width(cs), trm);
		printf("%s%s%s %s(%s)%s\n", CLR(trm, CBLD, sess->sessname),
		       CLR(trm, CBLD, sess->mp_short));
		list_paths_term(&sess, 1, clms_paths_sess, 1);

		break;
	}

	return 0;
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
		args.showmode_set = 1;
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
		ERR("please specify device or session to display\n");
		return i;
	}

	snprintf(args.name, sizeof(args.name), "%s", argv[j]);
	return j + 1;
}

static void help_show(struct cmd *cmd)
{
	cmd_print_usage(cmd, "<name> [path] ");

	printf("\nArguments:\n");
	print_opt("<name>", "Name of the local or remote block device,");
	print_opt("", " session name, path name or remote hostname.");
	print_opt("", "I.e. ibnbd0, /dev/ibnbd0, d12aef94-4110-4321-9373-3be8494a557b,"
		      " ps401a-1@st401b-2, st401b-2, <ip1>@<ip2>, etc.");
	print_opt("", "In order to display path information, path name or identifier");
	print_opt("", "has to be provided, i.e. st401b-2:1.");

	printf("\nOptions:\n");
	print_opt("{path}", "Path name (<addr>@<addr>) or address (<addr>),");
	print_opt("", "where addr can be of the form [ip:]<ipv4>, ip:<ipv6>, gid:<gid>");
	print_opt("{port}", "HCA port in the format \"port=<n>\"");
	help_fields();

	printf("%s%s%s%s\n\n", HPRE, CLR(trm, CUND, "Device fields:"));
	print_fields(def_clms_devices_clt, def_clms_devices_srv, all_clms_devices);

	printf("%s%s%s%s\n\n", HPRE, CLR(trm, CUND, "Sessions fields:"));
	print_fields(def_clms_sessions_clt, def_clms_sessions_srv, all_clms_sessions);

	printf("%s%s%s%s\n\n", HPRE, CLR(trm, CUND, "Paths fields:"));
	print_fields(def_clms_paths_clt, def_clms_paths_srv, all_clms_paths);

	printf("\n%sProvide 'all' to print all available fields\n", HPRE);

	print_opt("{format}", "Output format: csv|json|xml");
	print_opt("{unit}", "Units to use for size (in binary): B|K|M|G|T|P|E");
	print_opt("{mode}", "Information to print: device|session|path. Default: device.");

	print_sarg_descr("help");
}

static void help_map(struct cmd *cmd)
{
	cmd_print_usage(cmd, "<path> from <server>");

	printf("\nArguments:\n");
	print_opt("<path>", "Name of the device to be mapped");
	print_opt("from <server>", "Address, name or session name of the server");

	printf("\nOptions:\n");
	print_opt("{iomode}",
		  "IO Mode to use on server side: fileio|blockio. Default: blockio");
	print_opt("{rw}",
		  "Access permission on server side: ro|rw|migration. Default: rw");
	print_sarg_descr("verbose");
	print_sarg_descr("help");
}

static int cmd_map(void)
{
	printf("TODO\n");
	return 0;
}

static struct cmd cmds[] = {
	{"list", "List ibnbd block- and transport information",
		 "List ibnbd block- and transport information: "
		 "devices, sessions, paths, etc.", cmd_list, NULL, help_list},
	{"show", "Show information about a device, a session or a path",
		 "Show information about an ibnbd block- or transport- item: "
		 "device, session or path.", cmd_show, parse_name, help_show},
	{"map", "Map a device from a remote machine",
		 "Map a device from a remote machine",
		 cmd_map, parse_name, help_map},
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

static int parse_devices_clms(const char *arg)
{
	int rc_clt, rc_srv;

	rc_clt = table_extend_columns(arg, ",", all_clms_devices,
				      args.clms_devices_clt, CLM_MAX_CNT);

	rc_srv = table_extend_columns(arg, ",", all_clms_devices,
				      args.clms_devices_srv, CLM_MAX_CNT);

	return rc_clt && rc_srv;
}

static int parse_sessions_clms(const char *arg)
{
	int rc_clt, rc_srv;

	rc_clt = table_extend_columns(arg, ",", all_clms_sessions,
				      args.clms_sessions_clt, CLM_MAX_CNT);

	rc_srv = table_extend_columns(arg, ",", all_clms_sessions,
				      args.clms_sessions_srv, CLM_MAX_CNT);
	return rc_clt && rc_srv;
}

static int parse_paths_clms(const char *arg)
{
	int rc_clt, rc_srv;

	rc_clt = table_extend_columns(arg, ",", all_clms_paths,
				      args.clms_paths_clt, CLM_MAX_CNT);

	rc_srv = table_extend_columns(arg, ",", all_clms_paths,
				      args.clms_paths_srv, CLM_MAX_CNT);
	return rc_clt && rc_srv;
}

static void init_args(void)
{
	memcpy(&args.clms_devices_clt, &def_clms_devices_clt,
	       DEF_CLMS_DEVICES_CLT_CNT * sizeof(all_clms_devices[0]));
	memcpy(&args.clms_devices_srv, &def_clms_devices_srv,
	       DEF_CLMS_DEVICES_SRV_CNT * sizeof(all_clms_devices[0]));

	memcpy(&args.clms_sessions_clt, &def_clms_sessions_clt,
	       DEF_CLMS_SESSIONS_CLT_CNT * sizeof(all_clms_sessions[0]));
	memcpy(&args.clms_sessions_srv, &def_clms_sessions_srv,
	       DEF_CLMS_SESSIONS_SRV_CNT * sizeof(all_clms_sessions[0]));

	memcpy(&args.clms_paths_clt, &def_clms_paths_clt,
	       DEF_CLMS_PATHS_CLT_CNT * sizeof(all_clms_paths[0]));
	memcpy(&args.clms_paths_srv, &def_clms_paths_srv,
	       DEF_CLMS_PATHS_SRV_CNT * sizeof(all_clms_paths[0]));
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
		DIR* dir;

		dir = opendir("/sys/module/ibnbd_client");
		if (dir) {
			args.ibnbdmode |= IBNBD_CLIENT;
			args.ibnbdmode_set = 1;
			closedir(dir);
		}
		dir = opendir("/sys/module/ibnbd_server");
		if (dir) {
			args.ibnbdmode |= IBNBD_SERVER;
			args.ibnbdmode_set = 1;
			closedir(dir);
		}
		if (!args.ibnbdmode) {
			args.ibnbdmode |= IBNBD_BOTH;
			args.ibnbdmode_set = 1;
		}
	}
}

int main(int argc, char **argv)
{
	int ret = 0, i, rcd, rcs, rcp;
	struct sarg *sarg;
	struct cmd *cmd;

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
			rcd = parse_devices_clms(argv[i]);
			rcs = parse_sessions_clms(argv[i]);
			rcp = parse_paths_clms(argv[i]);
			if (!parse_precision(argv[i]) ||
			    !(rcd && rcs && rcp)) {
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
	else if (cmd->func) {
		if (args.ibnbdmode == IBNBD_NONE) {
			ERR("ibnbd modules not loaded\n");
			ret = -ENOENT;
			goto out;
		}
		ret = cmd->func();
	}

out:
	return ret;
}
