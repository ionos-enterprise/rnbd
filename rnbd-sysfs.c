// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Configuration tool for RNBD driver and RTRS library.
 *
 * Copyright (c) 2019 1&1 IONOS SE. All rights reserved.
 * Authors: Danil Kipnis <danil.kipnis@cloud.ionos.com>
 *          Lutz Pogrell <lutz.pogrell@cloud.ionos.com>
 */

#include <errno.h>
#include <string.h>	/* for strdup() */
#include <stdio.h>	/* for printf() */
#include <stdlib.h>	/* for free() */
#include <sys/stat.h>	/* for stat() */
#include <sys/types.h>	/* for open() */
#include <fcntl.h>	/* for open() */
#include <dirent.h>	/* for opendir() */
#include <unistd.h>	/* for write() */
#include <stdarg.h>
#include <libgen.h>	/* for basename */
#include <inttypes.h>
#include <stdbool.h>

#include "rnbd-sysfs.h"
#include "table.h"
#include "misc.h"

#define PATH_DEV_CLT          "/sys/class/rnbd-client/ctl"
#define PATH_SESS_CLT         "/sys/class/rtrs-client/"
#define PATH_DEV_SRV          "/sys/class/rnbd-server/ctl"
#define PATH_SESS_SRV         "/sys/class/rtrs-server/"
#define PATH_DEV_NAME         "rnbd"

#define COMPAT_PATH_DEV_CLT   "/sys/class/ibnbd-client/ctl"
#define COMPAT_PATH_SESS_CLT  "/sys/class/ibtrs-client/"
#define COMPAT_PATH_DEV_SRV   "/sys/class/ibnbd-server/ctl"
#define COMPAT_PATH_SESS_SRV  "/sys/class/ibtrs-server/"
#define COMPAT_PATH_DEV_NAME  "ibnbd"

struct rnbd_dev *devs[4096]; /* FIXME: this has to be a list */


static struct rnbd_sysfs_paths _sysfs_paths =
{
	.path_dev_clt = PATH_DEV_CLT,
	.path_sess_clt = PATH_SESS_CLT,
	.path_dev_srv = PATH_DEV_SRV,
	.path_sess_srv = PATH_SESS_SRV
};

static struct rnbd_sysfs_paths _compat_sysfs_paths =
{
	.path_dev_clt = COMPAT_PATH_DEV_CLT,
	.path_sess_clt = COMPAT_PATH_SESS_CLT,
	.path_dev_srv = COMPAT_PATH_DEV_SRV,
	.path_sess_srv = COMPAT_PATH_SESS_SRV
};

const struct rnbd_sysfs_paths * use_sysfs_paths = &_sysfs_paths;

const struct rnbd_sysfs_paths * const
get_sysfs_paths(const struct rnbd_ctx *ctx)
{
	return use_sysfs_paths;
}

int printf_sysfs(const char *dir, const char *entry,
		 const struct rnbd_ctx *ctx, const char *format, ...)
{
	char path[PATH_MAX];
	char cmd[4096];
	va_list args;
	FILE *f;
	int ret;

	snprintf(path, sizeof(path), "%s/%s", dir, entry);

	va_start(args, format);
	ret = vsnprintf(cmd, sizeof(cmd), format, args);
	va_end(args);

	if (ctx->debug_set || ctx->simulate_set) {

		printf("echo '%s' > %s\n", cmd, path);
		if (ctx->simulate_set)
			return 0;
	}
	f = fopen(path, "w");
	if (!f)
		return -errno;

	ret = fprintf(f, cmd, args);

	if (ret >= 0) {
		if (fflush(f))
			ret = -errno;
		else
			ret = 0;
	}
	/* if something failed flush should have reported, */
	/* don't need to check return value of close. */
	fclose(f);

	return ret;
}

int scanf_sysfs(const char *dir, const char *entry, const char *format, ...)
{
	char path[PATH_MAX];
	va_list args;
	FILE *f;
	int ret;

	snprintf(path, sizeof(path), "%s/%s", dir, entry);

	f = fopen(path, "r");
	if (!f)
		return -1;

	va_start(args, format);
	ret = vfscanf(f, format, args);
	va_end(args);

	fclose(f);

	return ret;
}

static void rnbd_sysfs_free(struct rnbd_sess_dev **sds,
			     struct rnbd_sess **sess,
			     struct rnbd_path **paths)
{
	int i;

	for (i = 0; sds[i]; i++)
		free(sds[i]);
	free(sds);

	for (i = 0; sess[i]; i++) {
		free(sess[i]->paths);
		free(sess[i]);
	}
	free(sess);

	for (i = 0; paths[i]; i++)
		free(paths[i]);
	free(paths);
}

void rnbd_sysfs_free_all(struct rnbd_sess_dev **sds_clt,
			  struct rnbd_sess_dev **sds_srv,
			  struct rnbd_sess **sess_clt,
			  struct rnbd_sess **sess_srv,
			  struct rnbd_path **paths_clt,
			  struct rnbd_path **paths_srv)
{
	int i;

	rnbd_sysfs_free(sds_clt, sess_clt, paths_clt);
	rnbd_sysfs_free(sds_srv, sess_srv, paths_srv);

	for (i = 0; devs[i]; i++)
		free(devs[i]);
}

static int dir_cnt(const char *dir)
{
	struct dirent *entry;
	int cnt = 0;
	DIR *dirp;

	dirp = opendir(dir);
	if (!dirp)
		return 0;

	for (entry = readdir(dirp); entry; entry = readdir(dirp))
		if (entry->d_name[0] != '.')
			cnt++;

	closedir(dirp);

	return cnt;
}

static int rnbd_sysfs_path_cnt(const char *sdir)
{
	struct dirent *sess;
	char pdir[PATH_MAX];
	int cnt = 0;
	DIR *sp;

	sp = opendir(sdir);
	if (!sp)
		return 0;

	for (sess = readdir(sp); sess; sess = readdir(sp)) {
		if (sess->d_name[0] == '.')
			continue;

		sprintf(pdir, "%s/%s/paths/", sdir, sess->d_name);
		cnt += dir_cnt(pdir);
	}
	closedir(sp);

	return cnt;
}

static int rnbd_sysfs_sds_srv_cnt(void)
{
	struct dirent *sd;
	char dir[PATH_MAX];
	int cnt = 0;
	DIR *d;

	sprintf(dir, "%s/devices/", use_sysfs_paths->path_dev_srv);
	d = opendir(dir);
	if (!d)
		return 0;

	for (sd = readdir(d); sd; sd = readdir(d)) {
		if (sd->d_name[0] == '.')
			continue;

		sprintf(dir, "%s/devices/%s/sessions/", use_sysfs_paths->path_dev_srv, sd->d_name);
		cnt += dir_cnt(dir);
	}
	closedir(d);

	return cnt;
}

static int rnbd_sysfs_alloc(struct rnbd_sess_dev ***sds,
			     struct rnbd_sess ***sess,
			     struct rnbd_path ***paths,
			     int sds_cnt, int *sess_cnt, int *path_cnt,
			     const char *sess_path)
{
	*sess_cnt = dir_cnt(sess_path);
	if (*sess_cnt < 0)
		return *sess_cnt;

	*path_cnt = rnbd_sysfs_path_cnt(sess_path);
	if (*path_cnt < 0)
		return *path_cnt;

	*sess_cnt += 1;
	*path_cnt += 1;

	*sds = calloc(sds_cnt, sizeof(*sds));
	if (!*sds)
		return -ENOMEM;

	*sess = calloc(*sess_cnt, sizeof(*sess));
	if (!*sess) {
		free(*sds);
		return -ENOMEM;
	}

	*paths = calloc(*path_cnt, sizeof(*paths));
	if (!*paths) {
		free(*sds);
		free(*sess);
		return -ENOMEM;
	}

	return 0;
}

int rnbd_sysfs_alloc_all(struct rnbd_sess_dev ***sds_clt,
			  struct rnbd_sess_dev ***sds_srv,
			  struct rnbd_sess ***sess_clt,
			  struct rnbd_sess ***sess_srv,
			  struct rnbd_path ***paths_clt,
			  struct rnbd_path ***paths_srv,
			  int *sds_clt_cnt, int *sds_srv_cnt,
			  int *sess_clt_cnt, int *sess_srv_cnt,
			  int *paths_clt_cnt, int *paths_srv_cnt)
{
	char tmp[PATH_MAX];
	int ret = 0;

	devs[0] = NULL;


	sprintf(tmp, "%s/devices/", use_sysfs_paths->path_dev_clt);
	*sds_clt_cnt = dir_cnt(tmp);
	if (*sds_clt_cnt < 0)
		return *sds_clt_cnt;


	*sds_srv_cnt = rnbd_sysfs_sds_srv_cnt();
	if (*sds_srv_cnt < 0)
		return *sds_srv_cnt;


	*sds_clt_cnt += 1;
	*sds_srv_cnt += 1;

	ret = rnbd_sysfs_alloc(sds_clt, sess_clt, paths_clt,
				*sds_clt_cnt, sess_clt_cnt, paths_clt_cnt,
				use_sysfs_paths->path_sess_clt);
	if (ret)
		return ret;

	ret = rnbd_sysfs_alloc(sds_srv, sess_srv, paths_srv,
				*sds_srv_cnt, sess_srv_cnt, paths_srv_cnt,
				use_sysfs_paths->path_sess_srv);
	if (ret)
		rnbd_sysfs_free(*sds_clt, *sess_clt, *paths_clt);

	return ret;
}

static struct rnbd_dev *find_or_add_dev(const char *syspath,
					 struct rnbd_dev **devs,
					 enum rnbdmode side)
{
	char *devname, *r, tmp[PATH_MAX], rpath[PATH_MAX];
	int i;

	strcpy(tmp, syspath);
	r = realpath(tmp, rpath);
	if (!r)
		return NULL;

	devname = basename(rpath);

	for (i = 0; devs[i]; i++)
		if (!strcmp(devname, devs[i]->devname))
			return devs[i];

	devs[i] = calloc(1, sizeof(**devs));
	if (!devs[i])
		return NULL;

	devs[i + 1] = NULL;

	strcpy(devs[i]->devname, devname);
	sprintf(devs[i]->devpath, "/dev/%s", devname);
	scanf_sysfs(rpath, "stat", "%*d %*d %d %*d %*d %*d %d", &devs[i]->rx_sect,
		    &devs[i]->tx_sect);

	if (side == RNBD_CLIENT) {
		strcat(rpath, "/ibnbd/");
		scanf_sysfs(rpath, "state", "%s", devs[i]->state);
	}

	return devs[i];
}

static struct rnbd_path *add_path(const char *sdir,
				   const char *pname,
				   struct rnbd_path **paths)
{
	struct rnbd_path *p;
	char ppath[PATH_MAX];
	int i;

	strcpy(ppath, sdir);
	strcat(ppath, pname);

	for (i = 0; paths[i]; i++)
		;

	p = calloc(1, sizeof(**paths));
	if (!p)
		return NULL;
	paths[i] = p;

	strcpy(p->pathname, pname);
	scanf_sysfs(ppath, "src_addr", "%s", p->src_addr);
	scanf_sysfs(ppath, "dst_addr", "%s", p->dst_addr);
	scanf_sysfs(ppath, "hca_name", "%s", p->hca_name);
	scanf_sysfs(ppath, "hca_port", "%d", &p->hca_port);
	scanf_sysfs(ppath, "state", "%s", p->state);

	scanf_sysfs(ppath, "/stats/rdma", "%*llu %llu %*llu %llu %d %d",
		    &p->rx_bytes, &p->tx_bytes, &p->inflights, &p->reconnects);

	return p;
}

static struct rnbd_sess *find_or_add_sess(const char *sessname,
					   struct rnbd_sess **sess,
					   struct rnbd_path **paths,
					   enum rnbdmode side)
{
	struct rnbd_sess *s;
	struct rnbd_path *p;
	struct dirent *pent;
	char tmp[PATH_MAX];
	DIR *pdir;
	int i;

	if (side == RNBD_CLIENT)
		sprintf(tmp, "%s%s", use_sysfs_paths->path_sess_clt, sessname);
	else
		sprintf(tmp, "%s%s", use_sysfs_paths->path_sess_clt, sessname);

	for (i = 0; sess[i]; i++)
		if (!strcmp(sessname, sess[i]->sessname))
			return sess[i];

	s = calloc(1, sizeof(**sess));
	if (!s)
		return NULL;

	sess[i] = s;

	strcpy(s->sessname, sessname);
	s->side = side;
	scanf_sysfs(tmp, "mpath_policy", "%s (%2s: %*d)", s->mp, s->mp_short);

	strcat(tmp, "/paths/");
	s->path_cnt = dir_cnt(tmp);
	if (!s->path_cnt)
		return s;

	s->paths = calloc(s->path_cnt + 1, sizeof(*paths));
	if (!s->paths) {
		free(s);
		return NULL;
	}

	pdir = opendir(tmp);
	if (!pdir)
		goto out;

	for (pent = readdir(pdir), i = 0; pent && i < s->path_cnt;
	     pent = readdir(pdir), i++) {
		if (pent->d_name[0] == '.') {
			i--;
			continue;
		}

		p = add_path(tmp, pent->d_name, paths);
		if (!p)
			goto out;

		s->paths[i] = p;
		p->sess = s;
		if (!strcmp(p->state, "connected")) {
			strcat(s->path_uu, "U");
			s->act_path_cnt++;
		} else {
			strcat(s->path_uu, "_");
		}

		s->rx_bytes += p->rx_bytes;
		s->tx_bytes += p->tx_bytes;
		s->inflights += p->inflights;
		s->reconnects += p->reconnects;
	}

	s->paths[i] = NULL;
	closedir(pdir);
	return s;

out:
	for (i = 0; s->paths[i]; i++)
		free(s->paths[i]);

	free(s->paths);
	free(s);
	return NULL;
}

static struct rnbd_sess_dev *add_sess_dev(const char *devname,
					   struct rnbd_sess_dev **sds,
					   struct rnbd_sess *s,
					   struct rnbd_dev *d,
					   enum rnbdmode side)
{
	char tmp[PATH_MAX];
	int i;

	if (side == RNBD_CLIENT)
		sprintf(tmp, "%s/devices/%s/ibnbd/", use_sysfs_paths->path_dev_clt, devname);
	else
		sprintf(tmp, "%s/devices/%s/sessions/%s",
			use_sysfs_paths->path_sess_srv, devname, s->sessname);

	for (i = 0; sds[i]; i++)
		;

	sds[i] = calloc(1, sizeof(**sds));
	if (!sds[i])
		return NULL;

	scanf_sysfs(tmp, "mapping_path", "%s", sds[i]->mapping_path);
	scanf_sysfs(tmp, "access_mode", "%s", sds[i]->access_mode);

	sds[i]->sess = s;
	sds[i]->dev = d;

	return sds[i];
}

static int rnbd_sysfs_read_clt(struct rnbd_sess_dev **sds,
				struct rnbd_sess **sess,
				struct rnbd_path **paths,
				struct rnbd_dev **devs)
{
	char tmp[PATH_MAX], sessname[NAME_MAX];
	struct dirent *dent;
	struct rnbd_sess_dev *sd;
	struct rnbd_sess *s;
	struct rnbd_dev *d;
	DIR *ddir;

	sprintf(tmp, "%s/devices/", use_sysfs_paths->path_dev_clt);
	ddir = opendir(tmp);
	if (!ddir)
		return 0;

	for (dent = readdir(ddir); dent; dent = readdir(ddir)) {
		if (dent->d_name[0] == '.')
			continue;

		sprintf(tmp, "%s/devices/%s", use_sysfs_paths->path_dev_clt, dent->d_name);
		scanf_sysfs(tmp, "/ibnbd/session", "%s", sessname);

		s = find_or_add_sess(sessname, sess, paths, RNBD_CLIENT);
		if (!s)
			return -ENOMEM;

		d = find_or_add_dev(tmp, devs, RNBD_CLIENT);
		if (!d)
			return -ENOMEM;

		sd = add_sess_dev(dent->d_name, sds, s, d, RNBD_CLIENT);
		if (!sd)
			return -ENOMEM;
	}

	closedir(ddir);

	return 0;
}

static int rnbd_sysfs_read_srv_sess_path(
		struct rnbd_sess **sess,
		struct rnbd_path **paths)
{
	struct dirent *sess_ent;
	DIR *sp;

	sp = opendir(use_sysfs_paths->path_sess_srv);
	if (!sp)
		return 0;

	for (sess_ent = readdir(sp); sess_ent; sess_ent = readdir(sp)) {
		if (sess_ent->d_name[0] == '.')
			continue;

		 find_or_add_sess(sess_ent->d_name, sess, paths, RNBD_SERVER);
	}
	closedir(sp);

	return 0;
}

static int rnbd_sysfs_read_srv(struct rnbd_sess_dev **sds,
				struct rnbd_sess **sess,
				struct rnbd_path **paths,
				struct rnbd_dev **devs)
{
	char tmp[PATH_MAX];
	int res;
	struct dirent *dent, *sent;
	struct rnbd_sess_dev *sd;
	struct rnbd_sess *s;
	struct rnbd_dev *d;
	DIR *ddir, *sdir;

	res = rnbd_sysfs_read_srv_sess_path(sess, paths);
	if (res)
		return res;
	
	sprintf(tmp, "%s/devices/", use_sysfs_paths->path_dev_srv);
	ddir = opendir(tmp);
	if (!ddir)
		return 0;

	for (dent = readdir(ddir); dent; dent = readdir(ddir)) {
		if (dent->d_name[0] == '.')
			continue;

		sprintf(tmp, "%s/devices/%s/block_dev",
			use_sysfs_paths->path_dev_srv, dent->d_name);

		d = find_or_add_dev(tmp, devs, RNBD_SERVER);
		if (!d)
			return -ENOMEM;

		sprintf(tmp, "%s/devices/%s/sessions/",
			use_sysfs_paths->path_dev_srv, dent->d_name);
		sdir = opendir(tmp);
		if (!sdir)
			return 0;
		for (sent = readdir(sdir); sent; sent = readdir(sdir)) {
			if (sent->d_name[0] == '.')
				continue;
			s = find_or_add_sess(sent->d_name, sess, paths,
					     RNBD_SERVER);
			if (!s)
				return -ENOMEM;

			sd = add_sess_dev(dent->d_name, sds,
					  s, d, RNBD_SERVER);
			if (!sd)
				return -ENOMEM;
		}
		closedir(sdir);
	}

	closedir(ddir);

	return 0;
}

/*
 * Read all the stuff from sysfs.
 * Use rnbd_sysfs_alloc_all() before and rnbd_sysfs_free_all() after.
 */
int rnbd_sysfs_read_all(struct rnbd_sess_dev **sds_clt,
			 struct rnbd_sess_dev **sds_srv,
			 struct rnbd_sess **sess_clt,
			 struct rnbd_sess **sess_srv,
			 struct rnbd_path **paths_clt,
			 struct rnbd_path **paths_srv)
{
	int ret = 0;

	ret = rnbd_sysfs_read_clt(sds_clt, sess_clt, paths_clt, devs);
	if (ret)
		return ret;

	ret = rnbd_sysfs_read_srv(sds_srv, sess_srv, paths_srv, devs);

	return ret;
}

enum rnbdmode mode_for_host(void)
{
	enum rnbdmode mode = RNBD_NONE;

	if (faccessat(AT_FDCWD, use_sysfs_paths->path_dev_clt, F_OK, AT_EACCESS) == 0)
		mode |= RNBD_CLIENT;

	/* else we are not interested in any error diagnossis here  */
	/* if we can not deduce the mode, than we just know nothing */

	if (faccessat(AT_FDCWD, use_sysfs_paths->path_dev_srv, F_OK, AT_EACCESS) == 0)
		mode |= RNBD_SERVER;

	return mode;
}

/**
 * check whether to use sysfs names with "rnbd" or "ibnbd"
 *
 * If only one of both are present, use it.
 * But if none or both are pressent, use "rnbd".
 * However if the executable name is "ibnbd" use this in any case.
 */
void check_compat_sysfs(const struct rnbd_ctx *ctx)
{
	if ((faccessat(AT_FDCWD, PATH_DEV_CLT, F_OK, AT_EACCESS) == 0
	    || faccessat(AT_FDCWD, PATH_DEV_SRV, F_OK, AT_EACCESS) == 0)
	    && (strcmp(ctx->pname, COMPAT_PATH_DEV_NAME) != 0))
		/* default is already set */
		return;

	if ((faccessat(AT_FDCWD, COMPAT_PATH_DEV_CLT, F_OK, AT_EACCESS) == 0)
	    || (faccessat(AT_FDCWD, COMPAT_PATH_DEV_SRV, F_OK, AT_EACCESS) == 0)
	    || (strcmp(ctx->pname, COMPAT_PATH_DEV_NAME) == 0))

		use_sysfs_paths = &_compat_sysfs_paths;
}

const char *mode_to_string(enum rnbdmode mode)
{
	switch (mode) {
	case RNBD_NONE:
		return "none";
	case RNBD_CLIENT:
		return "client";
	case RNBD_SERVER:
		return "server";
	case RNBD_BOTH:
		return "both";
	}
	/* NOTREACHED */

	return "";
}
