// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Configuration tool for IBNBD driver and IBTRS library.
 *
 * Copyright (c) 2019 1&1 IONOS SE. All rights reserved.
 * Authors: Danil Kipnis <danil.kipnis@cloud.ionos.com>
 *          Lutz Pogrell <lutz.pogrell@cloud.ionos.com>
 */

#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <arpa/inet.h>  /* AF_INET */
#include <unistd.h>	/* for isatty() */
#include <stdbool.h>

#include "table.h"
#include "misc.h"

#include "ibnbd-sysfs.h"

const struct bit_str bits[] = {
	{"B", 0, "Byte"}, {"K", 10, "KiB"}, {"M", 20, "MiB"}, {"G", 30, "GiB"},
	{"T", 40, "TiB"}, {"P", 50, "PiB"}, {"E", 60, "EiB"}
};

int get_unit_index(const char *unit, int *index)
{
	int i;

	for (i = 0; i < ARRSIZE(bits); i++)
		if (!strcasecmp(bits[i].str, unit)) {
			*index = i;
			return 0;
		}

	return -ENOENT;
}

int i_to_str_unit(uint64_t d, char *str, size_t len, int unit, int prec)
{
	if (unit >= ARRSIZE(bits))
		return -ENOENT;

	if (!unit)
		return snprintf(str, len, "%" PRIu64, d);

	return snprintf(str, len, "%.*f", prec,
			(double)d / (1L << bits[unit].bits));
}

void trim(char *s)
{
	char *new = s;

	while (*s) {
		if (!isspace(*s))
			*new++ = *s;
		s++;
	}
	*new = *s;
}

int str_to_size(const char *str, uint64_t *size)
{
	char buff[NAME_MAX];
	char *unit = NULL;
	int index = 0;
	uint64_t num;
	int ret;

	strncpy(buff, str, NAME_MAX);
	trim(buff);
	ret = sscanf(buff, "%" SCNu64 "%ms", &num, &unit);
	if (ret < 1)
		return -1;

	if (ret == 2) {
		ret = get_unit_index(unit, &index);
		free(unit);
		if (ret)
			return ret;
	}

	*size = num << bits[index].bits;
	return 0;
}

int i_to_str(uint64_t d, char *str, size_t len, int prec)
{
	int i;

	for (i = ARRSIZE(bits) - 1; i > 0; i--)
		if (d >> bits[i].bits)
			break;
	if (!i)
		return snprintf(str, len, "%" PRIu64 "%s", d, bits[i].str);

	return snprintf(str, len, "%.*f%s", prec,
			(double)d / (1L << bits[i].bits), bits[i].str);
}

int sd_state_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		    enum color *clr, void *v, bool humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);

	if (!strcmp("open", sd->dev->state))
		*clr = CGRN;
	else
		*clr = CRED;

	return snprintf(str, len, "%s", sd->dev->state);
}

int byte_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		enum color *clr, void *v, bool humanize)
{
	*clr = CNRM;
	return i_to_byte_unit(str, len, ctx, *(uint64_t *)v, humanize);
}

int sdd_io_mode_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		       enum color *clr, void *v, bool humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);
	*clr = CNRM;

	return snprintf(str, len, "%s", sd->dev->io_mode);
}

int sd_devname_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		      enum color *clr, void *v, bool humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);

	*clr = CNRM;

	return snprintf(str, len, "%s", sd->dev->devname);
}

int sd_devpath_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		      enum color *clr, void *v, bool humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);

	*clr = CNRM;

	return snprintf(str, len, "%s", sd->dev->devpath);
}

int sd_rx_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		 enum color *clr, void *v, bool humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);

	*clr = CNRM;

	return i_to_byte_unit(str, len, ctx, sd->dev->rx_sect << 9, humanize);
}

int sd_tx_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		 enum color *clr, void *v, bool humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);

	*clr = CNRM;

	return i_to_byte_unit(str, len, ctx, sd->dev->tx_sect << 9, humanize);
}

int dev_sessname_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
			enum color *clr, void *v, bool humanize)
{
	struct ibnbd_sess_dev *sd = container_of(v, struct ibnbd_sess_dev,
						 sess);
	*clr = CNRM;

	if (!sd->sess)
		return 0;

	return snprintf(str, len, "%s", sd->sess->sessname);
}

int ibnbd_path_state_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
			    enum color *clr, void *v, bool humanize)
{
	struct ibnbd_path *p = container_of(v, struct ibnbd_path, state);

	if (!strcmp(p->state, "connected"))
		*clr = CGRN;
	else
		*clr = CRED;

	return snprintf(str, len, "%s", p->state);
}

static bool is_gid(const char *arg)
{
	if (strncmp("gid:", arg, 4) == 0)
		return true;
	else
		return false;
}

int ibnbd_addr_to_norm(char *str, size_t len, char *v)
{
	char addr[16];
	int cnt, af;

	if (is_gid(v))
		cnt = sprintf(str, "%s", "gid:");
	else
		cnt = sprintf(str, "%s", "ip:");

	if (inet_pton(af = AF_INET6, v + cnt,  addr) < 1)
		if (inet_pton(af = AF_INET, v + cnt, addr) < 1)
			goto out;

	if (!inet_ntop(af, addr, str + cnt, len - cnt))
		goto out;

	return strlen(str);

out:
	return snprintf(str, len, "%s", v);
}

int addr_to_norm(char *str, size_t len, const struct ibnbd_ctx *ctx,
		 enum color *clr, void *v, bool humanize)
{
	*clr = CNRM;

	if (!humanize)
		return snprintf(str, len, "%s", (char *) v);

	return ibnbd_addr_to_norm(str, len, v);
}

int path_to_sessname(char *str, size_t len, const struct ibnbd_ctx *ctx,
		     enum color *clr, void *v, bool humanize)
{
	struct ibnbd_path *p = container_of(v, struct ibnbd_path, sess);

	*clr = CNRM;

	if (p->sess)
		return snprintf(str, len, "%s", p->sess->sessname);
	else
		return snprintf(str, len, "%s", "");
}

int sd_sess_to_direction(char *str, size_t len, const struct ibnbd_ctx *ctx,
			 enum color *clr, void *v, bool humanize)
{
	struct ibnbd_sess_dev *p = container_of(v, struct ibnbd_sess_dev, sess);

	*clr = CNRM;

	if (!p->sess)
		return 0;

	switch (p->sess->side) {
	case IBNBD_CLIENT:
		return snprintf(str, len, "import");
	case IBNBD_SERVER:
		return snprintf(str, len, "export");
	default:
		return snprintf(str, len, "?");
	}
}

int i_to_byte_unit(char *str, size_t len, const struct ibnbd_ctx *ctx,
		   uint64_t v, bool humanize)
{
	if (humanize)
		if (ctx->unit_set)
			return i_to_str_unit(v, str, len, ctx->unit_id,
					     ctx->prec);
		else
			return i_to_str(v, str, len, ctx->prec);
	else
		return snprintf(str, len, "%" PRIu64, v);
}

int path_to_shortdesc(char *str, size_t len, const struct ibnbd_ctx *ctx,
		      enum color *clr, void *v, bool humanize)
{
	struct ibnbd_path *p = container_of(v, struct ibnbd_path, sess);
	enum color c;

	*clr = CDIM;

	/* return if sess not set (this is the case if called on a total) */
	if (!p->sess)
		return 0;

	if (!p->state[0] || !strcmp(p->state, "connected"))
		c = CDIM;
	else
		c = CRED;

	return snprintf(str, len, "%s %d %s%s%s", p->hca_name, p->hca_port,
			CLR(ctx->trm, c, p->pathname));
}

int act_path_cnt_to_state(char *str, size_t len, const struct ibnbd_ctx *ctx,
			  enum color *clr, void *v, bool humanize)
{
	int act_path_cnt = *(int *)v;

	if (act_path_cnt) {
		*clr = CGRN;
		return snprintf(str, len, "connected");
	}

	*clr = CRED;

	return snprintf(str, len, "disconnected");
}


int sess_side_to_direction(char *str, size_t len, const struct ibnbd_ctx *ctx,
			   enum color *clr, void *v, bool humanize)
{
	struct ibnbd_sess *s = container_of(v, struct ibnbd_sess, side);

	*clr = CNRM;
	if (s->side == IBNBD_CLIENT)
		return snprintf(str, len, "outgoing");
	else
		return snprintf(str, len, "incoming");
}


int path_sess_to_direction(char *str, size_t len, const struct ibnbd_ctx *ctx,
			   enum color *clr, void *v, bool humanize)
{
	struct ibnbd_path *p = container_of(v, struct ibnbd_path, sess);

	*clr = CNRM;

	/* return if sess not set (this is the case if called on a total) */
	if (!p->sess)
		return 0;

	switch (p->sess->side) {
	case IBNBD_CLIENT:
		return snprintf(str, len, "outgoing");
	case IBNBD_SERVER:
		return snprintf(str, len, "incoming");
	default:
		return snprintf(str, len, "?");
	}
}

static bool is_ip(const char *arg)
{
	if (strncmp("ip:", arg, 3) == 0)
		return true;
	else
		return false;
}

static bool is_ipv4_addr(const char *arg)
{
	char addr[4];

	return inet_pton(AF_INET, arg, addr);
}

static bool is_ipv6_addr(const char *arg)
{
	char addr[16];

	return inet_pton(AF_INET6, arg, addr);
}

bool is_path_addr(const char *arg)
{
	if (is_gid(arg) && is_ipv6_addr(arg+4))
		return true;

	if (is_ip(arg) && (is_ipv4_addr(arg+3) || is_ipv6_addr(arg+3)))
		return true;

	return false;
}

/**
 * Evaluate whether both left and right string represent
 * equivalent IBNBD path addresses.
 *
 * Address strings start with either 'ip:' or 'gid:'
 * followed by an valid (IPv4 or IPv6) address
 * or a valid gid (which looks like a link local IPv6 address).
 *
 * If both are neither one or the other string comparison is used
 * as fallback.
 *
 * For both ip and gid two addresses match if they are representations
 * if the same 'normalized' ip address string.
 */
bool match_path_addr(const char *left, const char *right)
{
	bool left_is_gid;
	bool left_is_ip;
	bool right_is_ip;
	char left_addr[16];
	char right_addr[16];
	char left_normalized[INET6_ADDRSTRLEN];
	char right_normalized[INET6_ADDRSTRLEN];

	if (!left && !right)
		return true;

	if ((!left && right) || (left && !right))
		return false;

	if ((is_gid(left) && is_gid(right))
	    || (is_ip(left) && is_ip(right))) {

		left_is_gid = is_gid(left);
		left_is_ip = inet_pton(AF_INET6, left+(left_is_gid ? 4 : 3),
				       left_addr);
		right_is_ip = inet_pton(AF_INET6, right+(left_is_gid ? 4 : 3),
					right_addr);

		if ((!left_is_ip && right_is_ip)
		    || (left_is_ip && !right_is_ip))
			return false;

		if (left_is_ip) {

			inet_ntop(AF_INET6, left_addr, left_normalized,
				  INET6_ADDRSTRLEN);
			inet_ntop(AF_INET6, right_addr, right_normalized,
				  INET6_ADDRSTRLEN);

			return !strcmp(left_normalized, right_normalized);

		} else if (!left_is_gid) {
			left_is_ip = inet_aton(left+3,
					       (struct in_addr *)left_addr);
			right_is_ip = inet_aton(right+3,
						(struct in_addr *)right_addr);

			if ((!left_is_ip && right_is_ip)
			    || (left_is_ip && !right_is_ip))
				return false;

			if (left_is_ip) {

				inet_ntop(AF_INET, left_addr,
					  left_normalized, INET6_ADDRSTRLEN);
				inet_ntop(AF_INET, right_addr,
					  right_normalized, INET6_ADDRSTRLEN);

				return !strcmp(left_normalized,
					       right_normalized);
			}
		}
	}

	return !strcmp(left, right);
}
