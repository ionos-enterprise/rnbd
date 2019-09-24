// SPDX-License-Identifier: GPL-2.0-or-later
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

#include "ibnbd-sysfs.h"

struct ibnbd_dev *devs[4096]; /* FIXME: this has to be a list */

int printf_sysfs(const char *dir, const char *entry, const char *format, ...)
{
	char path[PATH_MAX];
	va_list args;
	FILE *f;
	int ret;

	snprintf(path, sizeof(path), "%s/%s", dir, entry);

	f = fopen(path, "w");
	if (!f)
		return -1;

	va_start(args, format);
	ret = vfprintf(f, format, args);
	va_end(args);

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

static void ibnbd_sysfs_free(struct ibnbd_sess_dev **sds,
			     struct ibnbd_sess **sess,
			     struct ibnbd_path **paths)
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

void ibnbd_sysfs_free_all(struct ibnbd_sess_dev **sds_clt,
			  struct ibnbd_sess_dev **sds_srv,
			  struct ibnbd_sess **sess_clt,
			  struct ibnbd_sess **sess_srv,
			  struct ibnbd_path **paths_clt,
			  struct ibnbd_path **paths_srv)
{
	int i;

	ibnbd_sysfs_free(sds_clt, sess_clt, paths_clt);
	ibnbd_sysfs_free(sds_srv, sess_srv, paths_srv);

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

static int ibnbd_sysfs_path_cnt(const char *sdir)
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

static int ibnbd_sysfs_sds_srv_cnt(void)
{
	struct dirent *sd;
	char sdir[PATH_MAX];
	int cnt = 0;
	DIR *d;

	d = opendir(PATH_SDS_SRV);
	if (!d)
		return 0;

	for (sd = readdir(d); sd; sd = readdir(d)) {
		if (sd->d_name[0] == '.')
			continue;

		sprintf(sdir, PATH_SDS_SRV "%s/sessions/", sd->d_name);
		cnt += dir_cnt(sdir);
	}
	closedir(d);

	return cnt;
}

static int ibnbd_sysfs_alloc(struct ibnbd_sess_dev ***sds,
			     struct ibnbd_sess ***sess,
			     struct ibnbd_path ***paths,
			     int sds_cnt, int *sess_cnt, int *path_cnt,
			     const char *sess_path)
{
	*sess_cnt = dir_cnt(sess_path);
	if (*sess_cnt < 0)
		return *sess_cnt;

	*path_cnt = ibnbd_sysfs_path_cnt(sess_path);
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

int ibnbd_sysfs_alloc_all(struct ibnbd_sess_dev ***sds_clt,
			  struct ibnbd_sess_dev ***sds_srv,
			  struct ibnbd_sess ***sess_clt,
			  struct ibnbd_sess ***sess_srv,
			  struct ibnbd_path ***paths_clt,
			  struct ibnbd_path ***paths_srv,
			  int *sds_clt_cnt, int *sds_srv_cnt,
			  int *sess_clt_cnt, int *sess_srv_cnt,
			  int *paths_clt_cnt, int *paths_srv_cnt)
{
	int ret = 0;

	devs[0] = NULL;


	*sds_clt_cnt = dir_cnt(PATH_SDS_CLT);
	if (*sds_clt_cnt < 0)
		return *sds_clt_cnt;


	*sds_srv_cnt = ibnbd_sysfs_sds_srv_cnt();
	if (*sds_srv_cnt < 0)
		return *sds_srv_cnt;


	*sds_clt_cnt += 1;
	*sds_srv_cnt += 1;

	ret = ibnbd_sysfs_alloc(sds_clt, sess_clt, paths_clt,
				*sds_clt_cnt, sess_clt_cnt, paths_clt_cnt,
				PATH_SESS_CLT);
	if (ret)
		return ret;

	ret = ibnbd_sysfs_alloc(sds_srv, sess_srv, paths_srv,
				*sds_srv_cnt, sess_srv_cnt, paths_srv_cnt,
				PATH_SESS_SRV);
	if (ret)
		ibnbd_sysfs_free(*sds_clt, *sess_clt, *paths_clt);

	return ret;
}

static struct ibnbd_dev *find_or_add_dev(const char *syspath,
					 struct ibnbd_dev **devs,
					 enum ibnbd_side side)
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
	scanf_sysfs(rpath, "stat", "%d %*d %*d %*d %d", &devs[i]->rx_sect,
		    &devs[i]->tx_sect);

	if (side == IBNBD_CLT) {
		strcat(rpath, "/ibnbd/");
		scanf_sysfs(rpath, "io_mode", "%s", devs[i]->io_mode);
		scanf_sysfs(rpath, "state", "%s", devs[i]->state);
	} else {
		scanf_sysfs(dirname(tmp), "io_mode", "%s", devs[i]->io_mode);
	}

	return devs[i];
}

static struct ibnbd_path *add_path(const char *sdir,
				   const char *pname,
				   struct ibnbd_path **paths)
{
	struct ibnbd_path *p;
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

static struct ibnbd_sess *find_or_add_sess(const char *sessname,
					   struct ibnbd_sess **sess,
					   struct ibnbd_path **paths,
					   enum ibnbd_side side)
{
	struct ibnbd_sess *s;
	struct ibnbd_path *p;
	struct dirent *pent;
	char tmp[PATH_MAX];
	DIR *pdir;
	int i;

	if (side == IBNBD_CLT)
		sprintf(tmp, PATH_SESS_CLT "%s", sessname);
	else
		sprintf(tmp, PATH_SESS_SRV "%s", sessname);

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

static struct ibnbd_sess_dev *add_sess_dev(const char *devname,
					   struct ibnbd_sess_dev **sds,
					   struct ibnbd_sess *s,
					   struct ibnbd_dev *d,
					   enum ibnbd_side side)
{
	char tmp[PATH_MAX];
	int i;

	if (side == IBNBD_CLT)
		sprintf(tmp, PATH_SDS_CLT "%s/ibnbd/", devname);
	else
		sprintf(tmp, PATH_SDS_SRV "%s/sessions/%s", devname,
			s->sessname);

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

static int ibnbd_sysfs_read_clt(struct ibnbd_sess_dev **sds,
				struct ibnbd_sess **sess,
				struct ibnbd_path **paths,
				struct ibnbd_dev **devs)
{
	char tmp[PATH_MAX], sessname[NAME_MAX];
	struct dirent *dent;
	struct ibnbd_sess_dev *sd;
	struct ibnbd_sess *s;
	struct ibnbd_dev *d;
	DIR *ddir;

	ddir = opendir(PATH_SDS_CLT);
	if (!ddir)
		return 0;

	for (dent = readdir(ddir); dent; dent = readdir(ddir)) {
		if (dent->d_name[0] == '.')
			continue;

		sprintf(tmp, PATH_SDS_CLT "%s", dent->d_name);
		scanf_sysfs(tmp, "/ibnbd/session", "%s", sessname);

		s = find_or_add_sess(sessname, sess, paths, IBNBD_CLT);
		if (!s)
			return -ENOMEM;

		d = find_or_add_dev(tmp, devs, IBNBD_CLT);
		if (!d)
			return -ENOMEM;

		sd = add_sess_dev(dent->d_name, sds, s, d, IBNBD_CLT);
		if (!sd)
			return -ENOMEM;
	}

	closedir(ddir);

	return 0;
}

static int ibnbd_sysfs_read_srv(struct ibnbd_sess_dev **sds,
				struct ibnbd_sess **sess,
				struct ibnbd_path **paths,
				struct ibnbd_dev **devs)
{
	char tmp[PATH_MAX];
	struct dirent *dent, *sent;
	struct ibnbd_sess_dev *sd;
	struct ibnbd_sess *s;
	struct ibnbd_dev *d;
	DIR *ddir, *sdir;

	ddir = opendir(PATH_SDS_SRV);
	if (!ddir)
		return 0;

	for (dent = readdir(ddir); dent; dent = readdir(ddir)) {
		if (dent->d_name[0] == '.')
			continue;

		sprintf(tmp, PATH_SDS_SRV "%s/block_dev", dent->d_name);

		d = find_or_add_dev(tmp, devs, IBNBD_SRV);
		if (!d)
			return -ENOMEM;

		sprintf(tmp, PATH_SDS_SRV "%s/sessions/", dent->d_name);
		sdir = opendir(tmp);
		if (!sdir)
			return 0;
		for (sent = readdir(sdir); sent; sent = readdir(sdir)) {
			if (sent->d_name[0] == '.')
				continue;
			s = find_or_add_sess(sent->d_name, sess, paths,
					     IBNBD_SRV);
			if (!s)
				return -ENOMEM;

			sd = add_sess_dev(dent->d_name, sds, s, d, IBNBD_SRV);
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
 * Use ibnbd_sysfs_alloc_all() before and ibnbd_sysfs_free_all() after.
 */
int ibnbd_sysfs_read_all(struct ibnbd_sess_dev **sds_clt,
			 struct ibnbd_sess_dev **sds_srv,
			 struct ibnbd_sess **sess_clt,
			 struct ibnbd_sess **sess_srv,
			 struct ibnbd_path **paths_clt,
			 struct ibnbd_path **paths_srv)
{
	int ret = 0;

	ret = ibnbd_sysfs_read_clt(sds_clt, sess_clt, paths_clt, devs);
	if (ret)
		return ret;

	ret = ibnbd_sysfs_read_srv(sds_srv, sess_srv, paths_srv, devs);

	return ret;
}
