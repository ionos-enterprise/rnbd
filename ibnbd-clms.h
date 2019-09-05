#include "table.h"

int i_to_byte_unit(char *str, size_t len, uint64_t v, int humanize);

static int byte_to_str(char *str, size_t len, enum color *clr, void *v,
		       int humanize)
{
	*clr = CNRM;

	return i_to_byte_unit(str, len, *(int *)v, humanize);
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
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);

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

static int sdd_io_mode_to_str(char *str, size_t len, enum color *clr, void *v,
			     int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);
	*clr = CNRM;

	return snprintf(str, len, "%s", sd->dev->io_mode);
}

CLM_SD(access_mode, "Access Mode", FLD_STR, sd_access_mode_to_str, 'l', CNRM,
	CNRM, "RW mode of the device: ro, rw or migration");

static int sd_devname_to_str(char *str, size_t len, enum color *clr,
			     void *v, int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);

	*clr = CNRM;

	return snprintf(str, len, "%s", sd->dev->devname);
}

static struct table_column clm_ibnbd_dev_devname =
	_CLM_SD("devname", sess, "Device", FLD_STR, sd_devname_to_str, 'l',
		CNRM, CNRM, "Device name under /dev/. I.e. ibnbd0");

static int sd_devpath_to_str(char *str, size_t len, enum color *clr,
			     void *v, int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);

	*clr = CNRM;

	return snprintf(str, len, "%s", sd->dev->devpath);
}

static struct table_column clm_ibnbd_dev_devpath =
	_CLM_SD("devpath", sess, "Device path", FLD_STR, sd_devpath_to_str, 'l',
		CNRM, CNRM, "Device path under /dev/. I.e. /dev/ibnbd0");

static struct table_column clm_ibnbd_dev_io_mode =
	_CLM_SD("io_mode", sess, "IO mode", FLD_STR,
		sdd_io_mode_to_str, 'l', CNRM, CNRM,
		"IO submission mode of the target device: file/block");

static int sd_rx_to_str(char *str, size_t len, enum color *clr, void *v,
		       int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);

	*clr = CNRM;

	return i_to_byte_unit(str, len, sd->dev->rx_sect << 9, humanize);
}

static struct table_column clm_ibnbd_dev_rx_sect =
	_CLM_SD("rx_sect", sess, "RX", FLD_NUM, sd_rx_to_str, 'l', CNRM, CNRM,
	"Amount of data read from the device");

static int sd_to_shortdesc(char *str, size_t len, enum color *clr,
			   void *v, int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);
	if (!sd->dev)
		return 0;

	*clr = CNRM;

	return snprintf(str, len, "%s (%s)", sd->mapping_path,
			sd->dev->devname);
}

static struct table_column clm_ibnbd_dev_shortdesc =
	_CLM_SD("shortdesc", sess, "Short", FLD_STR,
	       sd_to_shortdesc, 'l', CNRM, CNRM, "Short description");

static int sd_tx_to_str(char *str, size_t len, enum color *clr, void *v,
		       int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);

	*clr = CNRM;

	return i_to_byte_unit(str, len, sd->dev->tx_sect << 9, humanize);
}

static struct table_column clm_ibnbd_dev_tx_sect =
	_CLM_SD("tx_sect", sess, "TX", FLD_NUM, sd_tx_to_str, 'l', CNRM, CNRM,
	"Amount of data written to the device");


static struct table_column clm_ibnbd_dev_state =
	_CLM_SD("state", sess, "State", FLD_STR, sd_state_to_str, 'l', CNRM,
		CNRM, "State of the IBNBD device. (client only)");

static int dev_sessname_to_str(char *str, size_t len, enum color *clr,
			       void *v, int humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);
	*clr = CNRM;

	if (!sd->sess)
		return 0;

	return snprintf(str, len, "%s", sd->sess->sessname);
}

static struct table_column clm_ibnbd_sess_dev_sessname =
	_CLM_SD("sessname", sess, "Session", FLD_STR, dev_sessname_to_str, 'l',
		CNRM, CNRM, "Name of the IBTRS session of the device");

static int sd_sess_to_direction(char *str, size_t len, enum color *clr,
				void *v, int humanize)
{
	struct ibnbd_sess_dev *p = container_of(v, struct ibnbd_sess_dev, sess);

	*clr = CNRM;

	if (!p->sess)
		return 0;

	switch (p->sess->side) {
	case IBNBD_CLT:
		return snprintf(str, len, "import");
	case IBNBD_SRV:
		return snprintf(str, len, "export");
	default:
		assert(0);
	}
}

static struct table_column clm_ibnbd_sess_dev_direction =
	_CLM_SD("direction", sess, "Direction", FLD_STR,
		sd_sess_to_direction, 'l', CNRM, CNRM,
		"Direction of data transfer: imported or exported");

static struct table_column *all_clms_devices[] = {
	&clm_ibnbd_sess_dev_sessname,
	&clm_ibnbd_sess_dev_mapping_path,
	&clm_ibnbd_dev_devname,
	&clm_ibnbd_dev_devpath,
	&clm_ibnbd_dev_state,
	&clm_ibnbd_sess_dev_access_mode,
	&clm_ibnbd_dev_io_mode,
	&clm_ibnbd_dev_rx_sect,
	&clm_ibnbd_dev_tx_sect,
	&clm_ibnbd_sess_dev_direction,
	NULL
};

static struct table_column *all_clms_devices_clt[] = {
	&clm_ibnbd_sess_dev_sessname,
	&clm_ibnbd_sess_dev_mapping_path,
	&clm_ibnbd_dev_devname,
	&clm_ibnbd_dev_devpath,
	&clm_ibnbd_dev_state,
	&clm_ibnbd_sess_dev_access_mode,
	&clm_ibnbd_dev_io_mode,
	&clm_ibnbd_dev_rx_sect,
	&clm_ibnbd_dev_tx_sect,
	&clm_ibnbd_sess_dev_direction,
	NULL
};

static struct table_column *all_clms_devices_srv[] = {
	&clm_ibnbd_sess_dev_sessname,
	&clm_ibnbd_sess_dev_mapping_path,
	&clm_ibnbd_dev_devname,
	&clm_ibnbd_dev_devpath,
	&clm_ibnbd_sess_dev_access_mode,
	&clm_ibnbd_dev_io_mode,
	&clm_ibnbd_dev_rx_sect,
	&clm_ibnbd_dev_tx_sect,
	&clm_ibnbd_sess_dev_direction,
	NULL
};

static struct table_column *def_clms_devices_clt[] = {
	&clm_ibnbd_sess_dev_sessname,
	&clm_ibnbd_sess_dev_mapping_path,
	&clm_ibnbd_dev_devname,
	&clm_ibnbd_dev_state,
	&clm_ibnbd_sess_dev_access_mode,
	&clm_ibnbd_dev_io_mode,
	NULL
};

static struct table_column *def_clms_devices_srv[] = {
	&clm_ibnbd_sess_dev_sessname,
	&clm_ibnbd_sess_dev_mapping_path,
	&clm_ibnbd_dev_devname,
	&clm_ibnbd_sess_dev_access_mode,
	&clm_ibnbd_dev_io_mode,
	NULL
};

static struct table_column *clms_devices_shortdesc[] = {
	&clm_ibnbd_dev_shortdesc,
	NULL
};

#define CLM_S(m_name, m_header, m_type, tostr, align, h_clr, c_clr, m_descr) \
	CLM(ibnbd_sess, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
	    m_descr, sizeof(m_header) - 1, 0)

#define _CLM_S(s_name, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
	       m_descr) \
	_CLM(ibnbd_sess, s_name, m_name, m_header, m_type, tostr, align, \
	     h_clr, c_clr, m_descr, sizeof(m_header) - 1, 0)

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
CLM_S(rx_bytes, "RX", FLD_NUM, byte_to_str, 'r', CNRM, CNRM,
	"Bytes received");
CLM_S(tx_bytes, "TX", FLD_NUM, byte_to_str, 'r', CNRM, CNRM, "Bytes send");
CLM_S(inflights, "Inflights", FLD_NUM, NULL, 'r', CNRM, CNRM, "Inflights");
CLM_S(reconnects, "Reconnects", FLD_NUM, NULL, 'r', CNRM, CNRM, "Reconnects");
CLM_S(path_uu, "PS", FLD_STR, NULL, 'l', CNRM, CNRM,
	"Up (U) or down (_) state of every path");

static int act_path_cnt_to_state(char *str, size_t len, enum color *clr,
				    void *v, int humanize)
{
	int act_path_cnt = *(int *)v;

	if (act_path_cnt) {
		*clr = CGRN;
		return snprintf(str, len, "connected");
	}

	*clr = CRED;

	return snprintf(str, len, "disconnected");
}

static struct table_column clm_ibnbd_sess_state =
	_CLM_S("state", act_path_cnt, "State", FLD_STR,
		act_path_cnt_to_state, 'l', CNRM, CNRM,
		"State of the session.");

static int sess_side_to_direction(char *str, size_t len, enum color *clr,
				  void *v, int humanize)
{
	struct ibnbd_sess *s = container_of(v, struct ibnbd_sess, side);

	*clr = CNRM;
	if (s->side == IBNBD_CLT)
		return snprintf(str, len, "outgoing");
	else
		return snprintf(str, len, "incoming");
}

CLM_S(side, "Direction", FLD_STR, sess_side_to_direction, 'l', CNRM, CNRM,
	"Direction of the session: incoming or outgoing");

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
	&clm_ibnbd_sess_side,
	NULL
};

static struct table_column *all_clms_sessions_clt[] = {
	&clm_ibnbd_sess_sessname,
	&clm_ibnbd_sess_path_cnt,
	&clm_ibnbd_sess_act_path_cnt,
	&clm_ibnbd_sess_state,
	&clm_ibnbd_sess_path_uu,
	&clm_ibnbd_sess_mp,
	&clm_ibnbd_sess_mp_short,
	&clm_ibnbd_sess_rx_bytes,
	&clm_ibnbd_sess_tx_bytes,
	&clm_ibnbd_sess_reconnects,
	&clm_ibnbd_sess_side,
	NULL
};

static struct table_column *all_clms_sessions_srv[] = {
	&clm_ibnbd_sess_sessname,
	&clm_ibnbd_sess_path_cnt,
	&clm_ibnbd_sess_rx_bytes,
	&clm_ibnbd_sess_tx_bytes,
	&clm_ibnbd_sess_inflights,
	&clm_ibnbd_sess_side,
	NULL
};

static struct table_column *def_clms_sessions_clt[] = {
	&clm_ibnbd_sess_sessname,
	&clm_ibnbd_sess_state,
	&clm_ibnbd_sess_path_uu,
	&clm_ibnbd_sess_mp_short,
	&clm_ibnbd_sess_tx_bytes,
	&clm_ibnbd_sess_rx_bytes,
	&clm_ibnbd_sess_inflights,
	&clm_ibnbd_sess_reconnects,
	NULL
};

static struct table_column *def_clms_sessions_srv[] = {
	&clm_ibnbd_sess_sessname,
	&clm_ibnbd_sess_path_uu,
	&clm_ibnbd_sess_tx_bytes,
	&clm_ibnbd_sess_rx_bytes,
	&clm_ibnbd_sess_inflights,
	&clm_ibnbd_sess_reconnects,
	NULL
};

static struct table_column *clms_sessions_shortdesc[] = {
	&clm_ibnbd_sess_sessname,
	NULL
};

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
CLM_P(src_addr, "Client Addr", FLD_STR, NULL, 'l', CNRM, CNRM,
	"Client address of the path");
CLM_P(dst_addr, "Server Addr", FLD_STR, NULL, 'l', CNRM, CNRM,
	"Server address of the path");
CLM_P(hca_name, "HCA", FLD_STR, NULL, 'l', CNRM, CNRM, "HCA name");
CLM_P(hca_port, "Port", FLD_VAL, NULL, 'r', CNRM, CNRM, "HCA port");
CLM_P(rx_bytes, "RX", FLD_NUM, byte_to_str, 'r', CNRM, CNRM, "Bytes received");
CLM_P(tx_bytes, "TX", FLD_NUM, byte_to_str, 'r', CNRM, CNRM, "Bytes send");
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

#define _CLM_P(s_name, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
	       m_descr) \
	_CLM(ibnbd_path, s_name, m_name, m_header, m_type, tostr, align, \
	     h_clr, c_clr, m_descr, sizeof(m_header) - 1, 0)

static struct table_column clm_ibnbd_path_sessname =
	_CLM_P("sessname", sess, "Sessname", FLD_STR, path_to_sessname, 'l',
	       CNRM, CNRM, "Name of the session.");

int path_to_shortdesc(char *str, size_t len, enum color *clr,
		      void *v, int humanize);

static struct table_column clm_ibnbd_path_shortdesc =
	_CLM_P("shortdesc", sess, "Short", FLD_STR,
	       path_to_shortdesc, 'l', CNRM, CNRM, "Short description");

static int path_sess_to_direction(char *str, size_t len, enum color *clr,
				  void *v, int humanize)
{
	struct ibnbd_path *p = container_of(v, struct ibnbd_path, sess);

	*clr = CNRM;

	/* return if sess not set (this is the case if called on a total) */
	if (!p->sess)
		return 0;

	switch (p->sess->side) {
	case IBNBD_CLT:
		return snprintf(str, len, "outgoing");
	case IBNBD_SRV:
		return snprintf(str, len, "incoming");
	default:
		assert(0);
	}
}

static struct table_column clm_ibnbd_path_direction =
	_CLM_P("direction", sess, "Direction", FLD_STR,
	       path_sess_to_direction, 'l', CNRM, CNRM,
	       "Direction of the path: incoming or outgoing");

static struct table_column *all_clms_paths[] = {
	&clm_ibnbd_path_sessname,
	&clm_ibnbd_path_pathname,
	&clm_ibnbd_path_src_addr,
	&clm_ibnbd_path_dst_addr,
	&clm_ibnbd_path_hca_name,
	&clm_ibnbd_path_hca_port,
	&clm_ibnbd_path_state,
	&clm_ibnbd_path_rx_bytes,
	&clm_ibnbd_path_tx_bytes,
	&clm_ibnbd_path_inflights,
	&clm_ibnbd_path_reconnects,
	&clm_ibnbd_path_direction,
	NULL
};

static struct table_column *all_clms_paths_clt[] = {
	&clm_ibnbd_path_sessname,
	&clm_ibnbd_path_src_addr,
	&clm_ibnbd_path_dst_addr,
	&clm_ibnbd_path_hca_name,
	&clm_ibnbd_path_hca_port,
	&clm_ibnbd_path_state,
	&clm_ibnbd_path_rx_bytes,
	&clm_ibnbd_path_tx_bytes,
	&clm_ibnbd_path_inflights,
	&clm_ibnbd_path_reconnects,
	&clm_ibnbd_path_direction,
	NULL
};

static struct table_column *all_clms_paths_srv[] = {
	&clm_ibnbd_path_sessname,
	&clm_ibnbd_path_src_addr,
	&clm_ibnbd_path_dst_addr,
	&clm_ibnbd_path_hca_name,
	&clm_ibnbd_path_hca_port,
	&clm_ibnbd_path_rx_bytes,
	&clm_ibnbd_path_tx_bytes,
	&clm_ibnbd_path_inflights,
	&clm_ibnbd_path_direction,
	NULL
};

static struct table_column *def_clms_paths_clt[] = {
	&clm_ibnbd_path_sessname,
	&clm_ibnbd_path_hca_name,
	&clm_ibnbd_path_hca_port,
	&clm_ibnbd_path_dst_addr,
	&clm_ibnbd_path_state,
	&clm_ibnbd_path_tx_bytes,
	&clm_ibnbd_path_rx_bytes,
	&clm_ibnbd_path_inflights,
	&clm_ibnbd_path_reconnects,
	NULL
};

static struct table_column *def_clms_paths_srv[] = {
	&clm_ibnbd_path_sessname,
	&clm_ibnbd_path_hca_name,
	&clm_ibnbd_path_hca_port,
	&clm_ibnbd_path_src_addr,
	&clm_ibnbd_path_tx_bytes,
	&clm_ibnbd_path_rx_bytes,
	&clm_ibnbd_path_inflights,
	NULL
};

static struct table_column *clms_paths_sess_clt[] = {
	&clm_ibnbd_path_hca_name,
	&clm_ibnbd_path_hca_port,
	&clm_ibnbd_path_dst_addr,
	&clm_ibnbd_path_state,
	NULL
};

static struct table_column *clms_paths_sess_srv[] = {
	&clm_ibnbd_path_hca_name,
	&clm_ibnbd_path_hca_port,
	&clm_ibnbd_path_src_addr,
	NULL
};

static struct table_column *clms_paths_shortdesc[] = {
	&clm_ibnbd_path_shortdesc,
	NULL
};
