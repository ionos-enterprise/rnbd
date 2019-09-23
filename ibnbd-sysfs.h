#include <limits.h>

#define PATH_IBNBD_CLT "/sys/class/ibnbd-client/ctl/"
#define PATH_SDS_CLT PATH_IBNBD_CLT "devices/"
#define PATH_SESS_CLT "/sys/class/ibtrs-client/"
#define PATH_SDS_SRV "/sys/class/ibnbd-server/ctl/devices/"
#define PATH_SESS_SRV "/sys/class/ibtrs-server/"

enum ibnbd_side {
	IBNBD_CLT,
	IBNBD_SRV
};

/*
 * A block device exported or imported
 */
struct ibnbd_dev {
	char		devname[NAME_MAX]; /* file under /dev/ */
	char		devpath[PATH_MAX]; /* /dev/ibnbd<x>, /dev/ram<x> */
	char		io_mode[NAME_MAX]; /* access file/block */
	unsigned long	rx_sect;	   /* from /sys/block/../stats */
	unsigned long	tx_sect;	   /* from /sys/block/../stats */
	char		state[NAME_MAX];   /* ../ibnbd/state sysfs entry */
};

struct ibnbd_path {
	struct ibnbd_sess *sess;	      /* parent session */
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

struct ibnbd_sess {
	enum ibnbd_side	  side;			/* client or server side */
	char		  sessname[NAME_MAX];	/* session name */
	char		  mp[NAME_MAX];		/* multipath policy */
	char		  mp_short[NAME_MAX];	/* multipath policy short */

	/* fields calculated from the list of paths */
	int		  act_path_cnt;		/* active path count */
	char		  path_uu[NAME_MAX];	/* paths states str */
	unsigned long	  rx_bytes;
	unsigned long	  tx_bytes;
	int		  inflights;
	int		  reconnects;

	/* paths */
	int		  path_cnt;	/* path count */
	struct ibnbd_path **paths;	/* paths */
};

struct ibnbd_sess_dev {
	struct ibnbd_sess	*sess;			/* session */
	char			mapping_path[NAME_MAX]; /* name for mapping */
	char			access_mode[64];	/* ro/rw/migration */
	struct ibnbd_dev	*dev;			/* ibnbd block device */
};

void ibnbd_sysfs_free_all(struct ibnbd_sess_dev **sds_clt,
			  struct ibnbd_sess_dev **sds_srv,
			  struct ibnbd_sess **sess_clt,
			  struct ibnbd_sess **sess_srv,
			  struct ibnbd_path **paths_clt,
			  struct ibnbd_path **paths_srv);

int ibnbd_sysfs_alloc_all(struct ibnbd_sess_dev ***sds_clt,
			  struct ibnbd_sess_dev ***sds_srv,
			  struct ibnbd_sess ***sess_clt,
			  struct ibnbd_sess ***sess_srv,
			  struct ibnbd_path ***paths_clt,
			  struct ibnbd_path ***paths_srv,
			  int *sds_clt_cnt, int *sds_srv_cnt,
			  int *sess_clt_cnt, int *sess_srv_cnt,
			  int *paths_clt_cnt, int *paths_srv_cnt);
/*
 * Read all the stuff from sysfs.
 * Use ibnbd_sysfs_alloc_all() before and ibnbd_sysfs_free_all() after.
 */
int ibnbd_sysfs_read_all(struct ibnbd_sess_dev **sds_clt,
			 struct ibnbd_sess_dev **sds_srv,
			 struct ibnbd_sess **sess_clt,
			 struct ibnbd_sess **sess_srv,
			 struct ibnbd_path **paths_clt,
			 struct ibnbd_path **paths_srv);

int printf_sysfs(const char *dir, const char *entry, const char *format, ...);
int scanf_sysfs(const char *dir, const char *entry, const char *format, ...);
