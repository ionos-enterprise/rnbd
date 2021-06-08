/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <limits.h>

struct rnbd_sysfs_info {
	const char *path_dev_clt;
	const char *path_sess_clt;
	const char *path_dev_srv;
	const char *path_sess_srv;
	const char *path_dev_name;
};

enum rnbdmode {
	RNBD_NONE = 0,
	RNBD_CLIENT = 1,
	RNBD_SERVER = 1 << 1,
	RNBD_BOTH = RNBD_CLIENT | RNBD_SERVER,
};

/*
 * A block device exported or imported
 */
struct rnbd_dev {
	char		devname[NAME_MAX]; /* file under /dev/ */
	char		devpath[PATH_MAX]; /* /dev/rnbd<x>, /dev/ram<x> */
	unsigned long	rx_sect;	   /* from /sys/block/../stats */
	unsigned long	tx_sect;	   /* from /sys/block/../stats */
	char		state[NAME_MAX];   /* ../rnbd/state sysfs entry */
};

struct rnbd_path {
	struct rnbd_sess *sess;	      /* parent session */
	char		  pathname[NAME_MAX]; /* path appears in sysfs */
	char		  src_addr[NAME_MAX]; /* client address */
	char		  dst_addr[NAME_MAX]; /* server address */
	char		  hca_name[NAME_MAX]; /* hca name */
	int		  hca_port;	      /* hca port */
	char		  state[NAME_MAX];    /* state sysfs entry */
	/* stats/rdma */
	unsigned long	  rx_bytes;
	unsigned long	  tx_bytes;
	int		  inflights;
	int		  reconnects;
};

struct rnbd_sess {
	enum rnbdmode	  side;			/* client or server side */
	char		  sessname[NAME_MAX];	/* session name */
	char		  mp[NAME_MAX];		/* multipath policy */
	char		  mp_short[NAME_MAX];	/* multipath policy short */
	char		  hostname[NAME_MAX];	/* hostname of counterpart */

	/* fields calculated from the list of paths */
	int		  act_path_cnt;		/* active path count */
	char		  path_uu[NAME_MAX];	/* paths states str */
	unsigned long	  rx_bytes;
	unsigned long	  tx_bytes;
	int		  inflights;
	int		  reconnects;

	/* paths */
	int		  path_cnt;	/* path count */
	struct rnbd_path **paths;	/* paths */
};

struct rnbd_sess_dev {
	struct rnbd_sess	*sess;			/* session */
	char			mapping_path[NAME_MAX]; /* name for mapping */
	char			access_mode[64];	/* ro/rw/migration */
	struct rnbd_dev	*dev;			/* rnbd block device */
};

void rnbd_sysfs_free_all(struct rnbd_sess_dev **sds_clt,
			  struct rnbd_sess_dev **sds_srv,
			  struct rnbd_sess **sess_clt,
			  struct rnbd_sess **sess_srv,
			  struct rnbd_path **paths_clt,
			  struct rnbd_path **paths_srv);

int rnbd_sysfs_alloc_all(struct rnbd_sess_dev ***sds_clt,
			  struct rnbd_sess_dev ***sds_srv,
			  struct rnbd_sess ***sess_clt,
			  struct rnbd_sess ***sess_srv,
			  struct rnbd_path ***paths_clt,
			  struct rnbd_path ***paths_srv,
			  int *sds_clt_cnt, int *sds_srv_cnt,
			  int *sess_clt_cnt, int *sess_srv_cnt,
			  int *paths_clt_cnt, int *paths_srv_cnt);
/*
 * Read all the stuff from sysfs.
 * Use rnbd_sysfs_alloc_all() before and rnbd_sysfs_free_all() after.
 */
int rnbd_sysfs_read_all(struct rnbd_sess_dev **sds_clt,
			struct rnbd_sess_dev **sds_srv,
			struct rnbd_sess **sess_clt,
			struct rnbd_sess **sess_srv,
			struct rnbd_path **paths_clt,
			struct rnbd_path **paths_srv);

struct rnbd_ctx;

int printf_sysfs(const char *dir, const char *entry,
		 const struct rnbd_ctx *ctx, const char *format, ...);
int scanf_sysfs(const char *dir, const char *entry, const char *format, ...);

enum rnbdmode mode_for_host(void);
const char *mode_to_string(enum rnbdmode mode);

void check_compat_sysfs(struct rnbd_ctx *ctx);
const struct rnbd_sysfs_info * const
get_sysfs_info(const struct rnbd_ctx *ctx);
