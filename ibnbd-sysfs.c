#include <errno.h>
#include <string.h> /* for strdup() */
#include <stdio.h> /* for printf() */
#include <stdlib.h> /* for free() */
#include <sys/stat.h> /* for stat() */
#include <sys/types.h> /* for open() */
#include <fcntl.h> /* for open() */
#include <dirent.h> /* for opendir() */
#include <unistd.h> /* for write() */
#include <stdarg.h>

#include "ibnbd-sysfs.h"

struct stat st;

int printf_sysfs(char *dir, char *entry, const char *format, ...)
{
	char path[PATH_MAX];
	va_list args;
	FILE *f;
	int ret;

	snprintf(path, sizeof(path), "%s/%s", dir, entry);

	f = fopen(path,"w");
	if (!f)
		return -1;

	va_start(args, format);
	ret = vfprintf(f, format, args);
	va_end(args);

	fclose(f);

	return ret;
}

int scanf_sysfs(char *dir, char *entry, const char *format, ...)
{
	char path[PATH_MAX];
	va_list args;
	FILE *f;
	int ret;

	snprintf(path, sizeof(path), "%s/%s", dir, entry);

	f = fopen(path,"r");
	if (!f)
		return -1;

	va_start(args, format);
	ret = vfscanf(f, format, args);
	va_end(args);

	fclose(f);

	return ret;
}

/* get the row-th str in val, return to dst */
void get_str(char *src, char **dst, int row)
{
	int cnt = 1;

	*dst  = strtok(src, " ");
	while (dst && cnt++ < row)
		*dst = strtok(NULL, " ");
}

int set_sysnode(char *val, char *path, char *sysname)
{
	char sysfs_path[PATH_MAX];
	int fd, n;

	snprintf(sysfs_path, PATH_MAX, "%s/%s", path, sysname);
	if (stat(sysfs_path, &st) != 0) {
		printf("%s doesn't exist\n", sysfs_path);
		return -ENODEV;
	}
	fd = open(sysfs_path, O_RDWR);
	if (fd < 0) {
		printf("can't open %s errno = %s\n", sysfs_path, strerror(errno));
		return -1;
	}

        n = write(fd, val, strlen(val));
        if (n != strlen(val)) {
		printf("can't write %s errno = %s\n", sysfs_path, strerror(errno));
		close(fd);
                return -1;
	}

	close(fd);
	return 0;
}

int get_sysnode(char *val, char *path, char *sysname)
{
	char sysfs_path[PATH_MAX];
	int fd, n;

	snprintf(sysfs_path, PATH_MAX, "%s/%s", path, sysname);
	if (stat(sysfs_path, &st) != 0) {
		printf("%s doesn't exist\n", sysfs_path);
		return -ENODEV;
	}
	fd = open(sysfs_path, O_RDONLY);
	if (fd < 0) {
		printf("can't open %s\n", sysfs_path);
		return -1;
	}

	lseek(fd, 0, 0);
        n = read(fd, val, 1024);
        if (n <= 0 || n == 1024) {
		printf("can't read %s\n", sysfs_path);
		close(fd);
                return -1;
	}
	val[n] = 0;

	close(fd);
	return 0;
}

/*
 * path: like /sys/block/ibnbd0/
 */
int get_ibnbd_dev_sysfs(char *path, struct ibnbd_dev *dev)
{
	char val[1024], *val1, new_path[PATH_MAX], *ret;

	if (stat(path, &st) != 0) {
		printf("%s doesn't exist\n", path);
		return -ENODEV;
	}

	get_sysnode(val, path, "/stat");
	val1 = strdup(val);
	/* parse the return val, rx_sect is the first one and tx_sect is the 5th */
	get_str(val, &ret, 1);
	dev->rx_sect = atoi(ret);
	get_str(val1, &ret, 5);
	dev->rx_sect = atoi(ret);
	free(val1);

	/* path changed to /sys/block/ibnbd0/ibnbd/ */
	snprintf(new_path, PATH_MAX, "%s/%s", path, "ibnbd");

	memset(val, 0, 1024);
	get_sysnode(val, new_path, "io_mode");
	strcpy(dev->io_mode, val);

	memset(val, 0, 1024);
	get_sysnode(val, new_path, "state");
	strcpy(dev->state, val);

	return 1;
}

/*
 * path: like /sys/class/ibtrs-client/ or /sys/class/ibtrs-server/
 */
int get_ibnbd_session_sysfs(char *path, char *sess_name,
			    struct ibnbd_sess *sess, char **path_name)
{
	char val[1024], new_path[PATH_MAX];
	DIR *dir;
	struct dirent *entry;
	int i = 0;

	snprintf(new_path, PATH_MAX, "%s/%s", path, sess_name);
	if (stat(new_path, &st) != 0) {
		printf("%s doesn't exist\n", new_path);
		return -ENODEV;
	}

	/* now get the value under /sys/class/ibtrs-client/$sess_name */
	memset(val, 0, 1024);
	get_sysnode(val, new_path, "mpath_policy");
	strcpy(sess->mp, val);

	strcat(new_path, "/paths");
	/* list all the paths */
        if ((dir = opendir(new_path)) == NULL)
                perror("opendir() error");
        else {
                while ((entry = readdir(dir)) != NULL) {
                        if (entry->d_name[0] != '.') {
                                strcat(new_path, "/");
                                strcat(new_path, entry->d_name);

                                if (stat(new_path, &st) != 0)
                                        fprintf(stderr, "stat() error on %s: %s\n",
						new_path, strerror(errno));
                                else if (S_ISDIR(st.st_mode)) {
					path_name[i] = malloc(strlen(entry->d_name) * sizeof(char));
					strcpy(path_name[i++], entry->d_name);
				}
                        }
                }
                closedir(dir);
        }

	return i;
}

/*
 * sess_sysfs: session sysfs node (check session by call get_ibnbd_session_sysfs)
 *		like /sys/class/ibtrs-client/gjiang
 * path_name: path name which should exist under session folder
 */
int get_ibnbd_path_sysfs(char *sess_sysfs, char *path_name,
			 struct ibnbd_sess *sess, struct ibnbd_path *path)
{
	char val[1024], *val1, path_sysfs[PATH_MAX], *ret;

	snprintf(path_sysfs, PATH_MAX, "%s/paths/%s", sess_sysfs, path_name);
	if (stat(path_sysfs, &st) != 0) {
		printf("%s doesn't exist\n", path_sysfs);
		return -ENODEV;
	}

	/* get src_addr, dst_addr, hca_name, hca_port and state */
	memset(val, 0, 1024);
	get_sysnode(val, path_sysfs, "src_addr");
	strcpy(path->src_addr, val);

	memset(val, 0, 1024);
	get_sysnode(val, path_sysfs, "dst_addr");
	strcpy(path->dst_addr, val);

	memset(val, 0, 1024);
	get_sysnode(val, path_sysfs, "hca_name");
	strcpy(path->hca_name, val);

	memset(val, 0, 1024);
	get_sysnode(val, path_sysfs, "hca_port");
	path->hca_port = atoi(val);

	memset(val, 0, 1024);
	get_sysnode(val, path_sysfs, "state");
	strcpy(path->state, val);

	/* get rx_bytes, tx_bytes, inflights and reconnects from stats/rdma */
	memset(val, 0, 1024);
	get_sysnode(val, path_sysfs, "/stats/rdma");

	val1 = strdup(val);
	/* parse the return val to get rx_bytes, tx_bytes,
	 * inflights and reconnects */
	get_str(val1, &ret, 1);
	path->rx_bytes = atoi(ret);
	sess->rx_bytes += path->rx_bytes;

	val1 = strdup(val);
	get_str(val1, &ret, 2);
	path->tx_bytes = atoi(ret);
	sess->tx_bytes += path->tx_bytes;

	val1 = strdup(val);
	get_str(val1, &ret, 3);
	path->inflights = atoi(ret);
	sess->inflights += path->inflights;

	val1 = strdup(val);
	get_str(val1, &ret, 4);
	path->reconnects = atoi(ret);
	sess->reconnects += path->reconnects;
	free(val1);

	return 1;
}

int get_ibnbd_sess_dev_sysfs(char *path, struct ibnbd_dev *dev)
{
	return 0;
}

/* test set/get sysfs nodes */
int bla(int argc, char *argv[])
{
	int path_num;
	char **paths;
	int i;

	paths = malloc(sizeof(char *) * 1024);;
	memset(paths, 0, sizeof(char *) * 1024);

	struct ibnbd_dev *dev = malloc(sizeof(struct ibnbd_dev));
	get_ibnbd_dev_sysfs("/sys/block/ibnbd0", dev);
	free(dev);

	struct ibnbd_sess *sess = malloc(sizeof(struct ibnbd_sess));
	path_num = get_ibnbd_session_sysfs("/sys/class/ibtrs-client",
					   "gjiang", sess, paths);
	sess->path_cnt = path_num;

	struct ibnbd_path *path = malloc(sizeof(struct ibnbd_path));
	get_ibnbd_path_sysfs("/sys/class/ibtrs-client/gjiang",
			     "ip:10.50.100.63@ip:10.50.100.65", sess, path);
	for (i = 0; i < path_num; i++)
		free(paths[i]);
	free(paths);
	free(path);
	free(sess);

	return 1;
}

static void ibnbd_sysfs_free(struct ibnbd_sess_dev **sds,
			     struct ibnbd_sess **sess,
			     struct ibnbd_path **paths)
{
	int i;

	for (i = 0; sds[i]; i++) {
		free(sds[i]->dev);
		free(sds[i]);
	}
	free(sds);

	for (i = 0; sess[i]; i++)
		free(sess[i]);
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
	ibnbd_sysfs_free(sds_clt, sess_clt, paths_clt);
	ibnbd_sysfs_free(sds_srv, sess_srv, paths_srv);
}

static int ibnbd_sysfs_alloc(struct ibnbd_sess_dev **sds,
			     struct ibnbd_sess **sess,
			     struct ibnbd_path **paths,
			     const char *sds_path,
			     const char *sess_path)
{
	int ret = 0;

	return ret;
}

#define PATH_SDS_CLT "/sys/class/ibnbd-client/ctl/devices/"
#define PATH_SESS_CLT "/sys/class/ibtrs-client/"
#define PATH_SDS_SRV "/sys/class/ibnbd-server/ctl/devices/"
#define PATH_SESS_SRV "/sys/class/ibtrs-server/"

int ibnbd_sysfs_alloc_all(struct ibnbd_sess_dev **sds_clt,
			  struct ibnbd_sess_dev **sds_srv,
			  struct ibnbd_sess **sess_clt,
			  struct ibnbd_sess **sess_srv,
			  struct ibnbd_path **paths_clt,
			  struct ibnbd_path **paths_srv)
{
	int ret = 0;

	ret = ibnbd_sysfs_alloc(sds_clt, sess_clt, paths_clt,
				PATH_SDS_CLT, PATH_SESS_CLT);
	if (ret)
		return ret;

	ret = ibnbd_sysfs_alloc(sds_srv, sess_srv, paths_srv,
				PATH_SDS_SRV, PATH_SESS_SRV);
	if (ret)
		ibnbd_sysfs_free(sds_clt, sess_clt, paths_clt);

	return ret;
}

static int ibnbd_sysfs_read_clt(struct ibnbd_sess_dev **sds_clt,
				struct ibnbd_sess **sess_clt,
				struct ibnbd_path **paths_clt)
{
	int ret = 0;

	return ret;
}

static int ibnbd_sysfs_read_srv(struct ibnbd_sess_dev **sds_srv,
				struct ibnbd_sess **sess_srv,
				struct ibnbd_path **paths_srv)
{
	int ret = 0;

	return ret;
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

	ret = ibnbd_sysfs_read_clt(sds_clt, sess_clt, paths_clt);
	if (ret)
		return ret;

	ret = ibnbd_sysfs_read_srv(sds_srv, sess_srv, paths_srv);
	if (ret)
		ibnbd_sysfs_free(sds_clt, sess_clt, paths_clt);

	return ret;
}
