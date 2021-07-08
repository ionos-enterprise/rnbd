/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Configuration tool for RNBD driver and RTRS library.
 *
 * Copyright (c) 2019 1&1 IONOS SE. All rights reserved.
 * Authors: Danil Kipnis <danil.kipnis@cloud.ionos.com>
 *          Lutz Pogrell <lutz.pogrell@cloud.ionos.com>
 */

#include "table.h"
#include "misc.h"

#define CLM_SD(m_name, m_header, m_type, tostr, align, h_clr, c_clr, m_descr) \
	CLM(rnbd_sess_dev, m_name, m_header, m_type, tostr, align, h_clr,\
	    c_clr, m_descr, sizeof(m_header) - 1, 0)

#define _CLM_SD(s_name, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
		m_descr) \
	_CLM(rnbd_sess_dev, s_name, m_name, m_header, m_type, tostr, \
	    align, h_clr, c_clr, m_descr, sizeof(m_header) - 1, 0)

CLM_SD(mapping_path, "Mapping Path", FLD_STR, NULL, 'l', CNRM, CBLD,
	"Mapping name of the remote device");

CLM_SD(access_mode, "Access Mode", FLD_STR, NULL, 'l', CNRM,
	CNRM, "RW mode of the device: ro, rw or migration");

static struct table_column clm_rnbd_dev_devname =
	_CLM_SD("devname", sess, "Device", FLD_STR, sd_devname_to_str, 'l',
		CNRM, CNRM, "Device name under /dev/. I.e. rnbd0");

static struct table_column clm_rnbd_dev_devpath =
	_CLM_SD("devpath", sess, "Device path", FLD_STR, sd_devpath_to_str, 'l',
		CNRM, CNRM, "Device path under /dev/. I.e. /dev/rnbd0");

static struct table_column clm_rnbd_dev_rx_sect =
	_CLM_SD("rx_sect", sess, "RX", FLD_LLU, sd_rx_to_str, 'r', CNRM, CNRM,
	"Amount of data read from the device");

static struct table_column clm_rnbd_dev_tx_sect =
	_CLM_SD("tx_sect", sess, "TX", FLD_LLU, sd_tx_to_str, 'r', CNRM, CNRM,
	"Amount of data written to the device");

static struct table_column clm_rnbd_dev_state =
	_CLM_SD("state", sess, "State", FLD_STR, sd_state_to_str, 'l', CNRM,
		CNRM, "State of the RNBD device. (client only)");

static struct table_column clm_rnbd_sess_dev_sessname =
	_CLM_SD("sessname", sess, "Session", FLD_STR, dev_sessname_to_str, 'l',
		CNRM, CNRM, "Name of the RTRS session of the device");

static struct table_column clm_rnbd_sess_dev_direction =
	_CLM_SD("direction", sess, "Direction", FLD_STR,
		sd_sess_to_direction, 'l', CNRM, CNRM,
		"Direction of data transfer: imported or exported");

static struct table_column clm_rnbd_sess_dev_hostname =
	_CLM_SD("hostname", sess, "Hostname", FLD_STR,
		sd_sess_to_hostname, 'l', CNRM, CNRM,
		"Hostname of the remote peer");

static struct table_column *all_clms_devices[] = {
	&clm_rnbd_sess_dev_sessname,
	&clm_rnbd_sess_dev_mapping_path,
	&clm_rnbd_dev_devname,
	&clm_rnbd_dev_devpath,
	&clm_rnbd_dev_state,
	&clm_rnbd_sess_dev_access_mode,
	&clm_rnbd_dev_rx_sect,
	&clm_rnbd_dev_tx_sect,
	&clm_rnbd_sess_dev_direction,
	&clm_rnbd_sess_dev_hostname,
	NULL
};

static struct table_column *all_clms_devices_clt[] = {
	&clm_rnbd_sess_dev_sessname,
	&clm_rnbd_sess_dev_mapping_path,
	&clm_rnbd_dev_devname,
	&clm_rnbd_dev_devpath,
	&clm_rnbd_dev_state,
	&clm_rnbd_sess_dev_access_mode,
	&clm_rnbd_dev_rx_sect,
	&clm_rnbd_dev_tx_sect,
	&clm_rnbd_sess_dev_direction,
	&clm_rnbd_sess_dev_hostname,
	NULL
};

static struct table_column *all_clms_devices_srv[] = {
	&clm_rnbd_sess_dev_sessname,
	&clm_rnbd_sess_dev_mapping_path,
	&clm_rnbd_dev_devname,
	&clm_rnbd_dev_devpath,
	&clm_rnbd_sess_dev_access_mode,
	&clm_rnbd_dev_rx_sect,
	&clm_rnbd_dev_tx_sect,
	&clm_rnbd_sess_dev_direction,
	&clm_rnbd_sess_dev_hostname,
	NULL
};

static struct table_column *def_clms_devices_clt[] = {
	&clm_rnbd_sess_dev_sessname,
	&clm_rnbd_sess_dev_mapping_path,
	&clm_rnbd_dev_devname,
	&clm_rnbd_dev_state,
	&clm_rnbd_sess_dev_access_mode,
	NULL
};

static struct table_column *def_clms_devices_srv[] = {
	&clm_rnbd_sess_dev_sessname,
	&clm_rnbd_sess_dev_mapping_path,
	&clm_rnbd_dev_devname,
	&clm_rnbd_sess_dev_access_mode,
	NULL
};

#define CLM_S(m_name, m_header, m_type, tostr, align, h_clr, c_clr, m_descr) \
	CLM(rnbd_sess, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
	    m_descr, sizeof(m_header) - 1, 0)

#define _CLM_S(s_name, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
	       m_descr) \
	_CLM(rnbd_sess, s_name, m_name, m_header, m_type, tostr, align, \
	     h_clr, c_clr, m_descr, sizeof(m_header) - 1, 0)

CLM_S(sessname, "Session name", FLD_STR, NULL, 'l', CNRM, CBLD,
	"Name of the session");
CLM_S(hostname, "Hostname", FLD_STR, NULL, 'l', CNRM, CBLD,
	"Hostname of the counterpart");
CLM_S(mp_short, "MP", FLD_STR, NULL, 'l', CNRM, CNRM,
	"Multipath policy (short)");
CLM_S(mp, "MP Policy", FLD_STR, NULL, 'l', CNRM, CNRM,
	"Multipath policy");
CLM_S(path_cnt, "Path cnt", FLD_INT, NULL, 'r', CNRM, CNRM,
	"Number of paths");
CLM_S(act_path_cnt, "Act path cnt", FLD_INT, NULL, 'r', CNRM, CNRM,
	"Number of active paths");
CLM_S(rx_bytes, "RX", FLD_LLU, byte_to_str, 'r', CNRM, CNRM,
	"Bytes received");
CLM_S(tx_bytes, "TX", FLD_LLU, byte_to_str, 'r', CNRM, CNRM, "Bytes send");
CLM_S(inflights, "Inflights", FLD_INT, NULL, 'r', CNRM, CNRM, "Inflights");
CLM_S(reconnects, "Reconnects", FLD_INT, NULL, 'r', CNRM, CNRM, "Reconnects");
CLM_S(path_uu, "PS", FLD_STR, NULL, 'l', CNRM, CNRM,
	"Up (U) or down (_) state of every path");

static struct table_column clm_rnbd_sess_state =
	_CLM_S("state", act_path_cnt, "State", FLD_STR,
		act_path_cnt_to_state, 'l', CNRM, CNRM,
		"State of the session.");

static struct table_column clm_rnbd_sess_srvname =
	_CLM_S("srvname", sessname, "Server Name", FLD_STR,
		sessname_to_srvname, 'l', CNRM, CNRM,
		"Server name");

static struct table_column clm_rnbd_sess_side =
	_CLM_S("direction", side, "Direction", FLD_STR,
		sess_side_to_direction, 'l', CNRM, CNRM,
		"Direction of the session: incoming or outgoing");

static struct table_column *all_clms_sessions[] = {
	&clm_rnbd_sess_sessname,
	&clm_rnbd_sess_path_cnt,
	&clm_rnbd_sess_act_path_cnt,
	&clm_rnbd_sess_state,
	&clm_rnbd_sess_path_uu,
	&clm_rnbd_sess_mp,
	&clm_rnbd_sess_mp_short,
	&clm_rnbd_sess_rx_bytes,
	&clm_rnbd_sess_tx_bytes,
	&clm_rnbd_sess_inflights,
	&clm_rnbd_sess_reconnects,
	&clm_rnbd_sess_side,
	&clm_rnbd_sess_srvname,
	&clm_rnbd_sess_hostname,
	NULL
};

static struct table_column *all_clms_sessions_clt[] = {
	&clm_rnbd_sess_sessname,
	&clm_rnbd_sess_path_cnt,
	&clm_rnbd_sess_act_path_cnt,
	&clm_rnbd_sess_state,
	&clm_rnbd_sess_path_uu,
	&clm_rnbd_sess_mp,
	&clm_rnbd_sess_mp_short,
	&clm_rnbd_sess_rx_bytes,
	&clm_rnbd_sess_tx_bytes,
	&clm_rnbd_sess_inflights,
	&clm_rnbd_sess_reconnects,
	&clm_rnbd_sess_side,
	&clm_rnbd_sess_srvname,
	&clm_rnbd_sess_hostname,
	NULL
};

static struct table_column *all_clms_sessions_srv[] = {
	&clm_rnbd_sess_sessname,
	&clm_rnbd_sess_path_cnt,
	&clm_rnbd_sess_rx_bytes,
	&clm_rnbd_sess_tx_bytes,
	&clm_rnbd_sess_inflights,
	&clm_rnbd_sess_side,
	&clm_rnbd_sess_hostname,
	NULL
};

static struct table_column *def_clms_sessions_clt[] = {
	&clm_rnbd_sess_sessname,
	&clm_rnbd_sess_state,
	&clm_rnbd_sess_path_uu,
	&clm_rnbd_sess_mp_short,
	&clm_rnbd_sess_tx_bytes,
	&clm_rnbd_sess_rx_bytes,
	&clm_rnbd_sess_reconnects,
	NULL
};

static struct table_column *def_clms_sessions_srv[] = {
	&clm_rnbd_sess_sessname,
	&clm_rnbd_sess_path_cnt,
	&clm_rnbd_sess_tx_bytes,
	&clm_rnbd_sess_rx_bytes,
	&clm_rnbd_sess_inflights,
	NULL
};

#define CLM_P(m_name, m_header, m_type, tostr, align, h_clr, c_clr, m_descr) \
	CLM(rnbd_path, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
	    m_descr, sizeof(m_header) - 1, 0)

CLM_P(state, "State", FLD_STR, rnbd_path_state_to_str, 'l', CNRM, CBLD,
	"Name of the path");
CLM_P(pathname, "Path name", FLD_STR, path_to_norm, 'l', CNRM, CNRM,
	"Path name");
CLM_P(src_addr, "Client Addr", FLD_STR, addr_to_norm, 'l', CNRM, CNRM,
	"Client address of the path");
CLM_P(dst_addr, "Server Addr", FLD_STR, addr_to_norm, 'l', CNRM, CNRM,
	"Server address of the path");
CLM_P(hca_name, "HCA", FLD_STR, NULL, 'l', CNRM, CNRM, "HCA name");
CLM_P(hca_port, "Port", FLD_VAL, NULL, 'r', CNRM, CNRM, "HCA port");
CLM_P(rx_bytes, "RX", FLD_LLU, byte_to_str, 'r', CNRM, CNRM, "Bytes received");
CLM_P(tx_bytes, "TX", FLD_LLU, byte_to_str, 'r', CNRM, CNRM, "Bytes send");
CLM_P(inflights, "Inflights", FLD_INT, NULL, 'r', CNRM, CNRM, "Inflights");
CLM_P(reconnects, "Reconnects", FLD_INT, NULL, 'r', CNRM, CNRM, "Reconnects");

#define _CLM_P(s_name, m_name, m_header, m_type, tostr, align, h_clr, c_clr, \
	       m_descr) \
	_CLM(rnbd_path, s_name, m_name, m_header, m_type, tostr, align, \
	     h_clr, c_clr, m_descr, sizeof(m_header) - 1, 0)

static struct table_column clm_rnbd_path_sessname =
	_CLM_P("sessname", sess, "Sessname", FLD_STR, path_to_sessname, 'l',
	       CNRM, CNRM, "Name of the session.");

static struct table_column clm_rnbd_path_hostname =
	_CLM_P("hostname", sess, "Hostname", FLD_STR, path_to_hostname, 'l',
	       CNRM, CNRM, "Hostname of the remote peer");

static struct table_column clm_rnbd_path_shortdesc =
	_CLM_P("shortdesc", sess, "Short", FLD_STR,
	       path_to_shortdesc, 'l', CNRM, CNRM, "Short description");

static struct table_column clm_rnbd_path_direction =
	_CLM_P("direction", sess, "Direction", FLD_STR,
	       path_sess_to_direction, 'l', CNRM, CNRM,
	       "Direction of the path: incoming or outgoing");

static struct table_column *all_clms_paths[] = {
	&clm_rnbd_path_sessname,
	&clm_rnbd_path_pathname,
	&clm_rnbd_path_src_addr,
	&clm_rnbd_path_dst_addr,
	&clm_rnbd_path_hca_name,
	&clm_rnbd_path_hca_port,
	&clm_rnbd_path_state,
	&clm_rnbd_path_rx_bytes,
	&clm_rnbd_path_tx_bytes,
	&clm_rnbd_path_inflights,
	&clm_rnbd_path_reconnects,
	&clm_rnbd_path_direction,
	&clm_rnbd_path_hostname,
	NULL
};

static struct table_column *all_clms_paths_clt[] = {
	&clm_rnbd_path_sessname,
	&clm_rnbd_path_pathname,
	&clm_rnbd_path_src_addr,
	&clm_rnbd_path_dst_addr,
	&clm_rnbd_path_hca_name,
	&clm_rnbd_path_hca_port,
	&clm_rnbd_path_state,
	&clm_rnbd_path_rx_bytes,
	&clm_rnbd_path_tx_bytes,
	&clm_rnbd_path_inflights,
	&clm_rnbd_path_reconnects,
	&clm_rnbd_path_direction,
	&clm_rnbd_path_hostname,
	NULL
};

static struct table_column *all_clms_paths_srv[] = {
	&clm_rnbd_path_sessname,
	&clm_rnbd_path_pathname,
	&clm_rnbd_path_src_addr,
	&clm_rnbd_path_dst_addr,
	&clm_rnbd_path_hca_name,
	&clm_rnbd_path_hca_port,
	&clm_rnbd_path_rx_bytes,
	&clm_rnbd_path_tx_bytes,
	&clm_rnbd_path_inflights,
	&clm_rnbd_path_direction,
	&clm_rnbd_path_hostname,
	NULL
};

static struct table_column *def_clms_paths_clt[] = {
	&clm_rnbd_path_sessname,
	&clm_rnbd_path_hca_name,
	&clm_rnbd_path_hca_port,
	&clm_rnbd_path_dst_addr,
	&clm_rnbd_path_state,
	&clm_rnbd_path_tx_bytes,
	&clm_rnbd_path_rx_bytes,
	NULL
};

static struct table_column *def_clms_paths_srv[] = {
	&clm_rnbd_path_sessname,
	&clm_rnbd_path_hca_name,
	&clm_rnbd_path_hca_port,
	&clm_rnbd_path_src_addr,
	&clm_rnbd_path_tx_bytes,
	&clm_rnbd_path_rx_bytes,
	NULL
};

static struct table_column *clms_paths_sess_clt[] = {
	&clm_rnbd_path_hca_name,
	&clm_rnbd_path_hca_port,
	&clm_rnbd_path_dst_addr,
	&clm_rnbd_path_state,
	NULL
};

static struct table_column *clms_paths_sess_srv[] = {
	&clm_rnbd_path_hca_name,
	&clm_rnbd_path_hca_port,
	&clm_rnbd_path_src_addr,
	NULL
};

struct table_column *clms_paths_shortdesc[] = {
	&clm_rnbd_path_shortdesc,
	NULL
};
