
#include <limits.h>

enum ibnbd_iomode {
	IBNBD_BLOCKIO,
	IBNBD_FILEIO
};

enum ibnbd_access_mode {
	IBNBD_RO,
	IBNBD_RW,
	IBNBD_MIGRATION
};

/*
 * IBNBD block device, client side
 */
struct ibnbd_dev {
	char 		  devname[NAME_MAX]; /* file under /dev/ */
	char 		  devpath[NAME_MAX]; /* /dev/ibnbd<x> */
	enum ibnbd_iomode iomode;	     /* access file/block */
	int		  rx_sect;	     /* from /sys/block/../stats */
	int		  tx_sect;	     /* from /sys/block/../stats */
	char		  state[NAME_MAX];   /* ../ibnbd/state sysfs entry */
};

/*
 * Target block device, server side.
 */
struct ibnbd_tgtdev {
	char 		  devname[NAME_MAX]; /* file under /dev/ */
	char 		  devpath[NAME_MAX]; /* /dev/ibnbd<x> */
	enum ibnbd_iomode iomode;	     /* access file/block */
};

struct ibnbd_path {
	struct ibnbd_sess *sess;	      /* parent session */
	char		  pathname[NAME_MAX]; /* path appears in sysfs */
	char		  cltaddr[NAME_MAX];  /* client address */
	char		  srvaddr[NAME_MAX];  /* server address */
	char		  hca_name[NAME_MAX]; /* hca name */
	int		  hca_port;	      /* hca port */
	char		  state[NAME_MAX];    /* state sysfs entry */
	/* stats/rdma */
	unsigned long	  rx_bytes;
	unsigned long	  tx_bytes;
	int		  inflights;
};

struct ibnbd_sess {
	char sessname[NAME_MAX];	/* session name */
	int active_path_cnt;		/* active path count */
	int path_cnt;			/* path count */
	struct ibnbd_path *paths;	/* paths */
};

struct ibnbd_sess_dev {
	struct ibnbd_sess	*sess;			/* session */
	char			mapping_path[NAME_MAX]; /* name for mapping */
	enum access_mode	access_mode; 		/* ro/rw/migration */
	struct ibnbd_tgtdev	*tgtdev;		/* target device*/
	struct ibnbd_dev	dev;			/* ibnbd block device */
};

