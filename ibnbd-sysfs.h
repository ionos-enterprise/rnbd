
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

enum ibnbd_side {
	IBNBD_CLT,
	IBNBD_SRV
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
	int		  reconnects;
};

struct ibnbd_sess {
	enum ibnbd_side	  side;			/* client or server side */
	char		  sessname[NAME_MAX];	/* session name */
	char		  mp[NAME_MAX];		/* multipath policy */
	char		  mp_short[NAME_MAX];	/* multipath policy short */

	/* fields calculated from the list of paths */
	int		  act_path_cnt;	/* active path count */
	char		  path_uu[NAME_MAX]; /* paths states str */
	unsigned long	  rx_bytes;
	unsigned long	  tx_bytes;
	int		  inflights;
	int		  reconnects;

	/* paths */
	int 		  path_cnt;	/* path count */
	struct ibnbd_path *paths[];	/* paths */
};

enum ibnbd_exp_imp {
	IBNBD_EXPORT,
	IBNBD_IMPORT
};

struct ibnbd_sess_dev {
	struct ibnbd_sess	*sess;			/* session */
	char			mapping_path[NAME_MAX]; /* name for mapping */
	enum ibnbd_access_mode	access_mode; 		/* ro/rw/migration */
	struct ibnbd_dev	*dev;			/* ibnbd block device */
	enum ibnbd_exp_imp	dir;			/* is it src or dst ? */
};


/*
 * Fake example data. Should be partially read from sysfs and partially
 * calculated instead.
 */

static struct ibnbd_sess g_s, g_s1, g_s2;
static struct ibnbd_path g_p[] = {
	{.sess = &g_s,
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
	{.sess = &g_s,
	 .pathname = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d6@"
		     "gid:fe80:0000:0000:0000:0002:c903:0010:c0f6",
	 .cltaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d6",
	 .srvaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0f6",
	 .hca_name = "mlx4_0",
	 .hca_port = 2,
	 .state = "disconnected",
	 .tx_bytes = 0,
	 .rx_bytes = 0,
	 .inflights = 0,
	 .reconnects = 3
	},
	{.sess = &g_s1,
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
	},
	{.sess = &g_s2,
	 .pathname = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d5@"
		     "gid:fe80:0000:0000:0000:0002:c903:0010:c0f5",
	 .cltaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d5",
	 .srvaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0f5",
	 .state = "connected",
	 .hca_name = "mlx4_0",
	 .hca_port = 1,
	 .tx_bytes = 0,
	 .rx_bytes = 377000,
	 .inflights = 0,
	},
	{.sess = &g_s2,
	 .pathname = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d6@"
		     "gid:fe80:0000:0000:0000:0002:c903:0010:c0f6",
	 .cltaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0d6",
	 .srvaddr = "gid:fe80:0000:0000:0000:0002:c903:0010:c0f6",
	 .state = "connected",
	 .hca_name = "mlx4_0",
	 .hca_port = 2,
	 .tx_bytes = 1023,
	 .rx_bytes = 0,
	 .inflights = 100500,
	}
};

static struct ibnbd_path *g_paths[] = {
	&g_p[0],
	&g_p[1],
	&g_p[2],
	&g_p[3],
	&g_p[4],
};

static struct ibnbd_sess g_s = {
	.side	  = IBNBD_CLT,
	.sessname = "ps401a-3@st401b-3",
	.mp = "round-robin",
	.mp_short = "RR",
	.act_path_cnt = 1,
	.path_cnt = 2,
	.tx_bytes = 1023,
	.rx_bytes = 377000,
	.inflights = 100500,
	.reconnects = 5,
	.path_uu = "U_",
	.paths = {
		&g_p[0],
		&g_p[1]
	}
};

static struct ibnbd_sess g_s1 = {
	.side	  = IBNBD_CLT,
	.sessname = "ps401a-3@st401b-4",
	.mp = "min-inflight",
	.mp_short = "MI",
	.act_path_cnt = 1,
	.path_cnt = 1,
	.tx_bytes = 1023,
	.rx_bytes = 377000,
	.inflights = 100500,
	.reconnects = 5,
	.path_uu = "U",
	.paths = {
		&g_p[2]
	}
};

static struct ibnbd_sess g_s2 = {
	.side	  = IBNBD_SRV,
	.sessname = "ps401a-1@ps401a-3",
	.mp = "min-inflight",
	.mp_short = "MI",
	.act_path_cnt = 2,
	.path_cnt = 2,
	.tx_bytes = 1023,
	.rx_bytes = 377000,
	.inflights = 100500,
	.reconnects = 5,
	.path_uu = "UU",
	.paths = {
		&g_p[3],
		&g_p[4]
	}
};

static struct ibnbd_sess *g_sessions[] = {
	&g_s,
	&g_s1,
	&g_s2,
	NULL
};

static struct ibnbd_dev g_d[] = {
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
	{.devname = "ibnbd3",
	 .devpath = "/dev/ibnbd3",
	 .iomode = IBNBD_BLOCKIO,
	 .rx_sect = 1904,
	 .tx_sect = 23423,
	 .state = "open"
	},
	{.devname = "ram0",
	 .devpath = "/dev/ram0",
	 .iomode = IBNBD_BLOCKIO,
	 .rx_sect = 190,
	 .tx_sect = 2342,
	 .state = "closed"
	},
};

static struct ibnbd_sess_dev g_sess_devs[] = {
	{.mapping_path = "112b5fc0-91f5-4157-8603-777f8e733f1f",
	 .access_mode = IBNBD_RW,
	 .sess = &g_s,
	 .dev = &g_d[0]
	},
	{.mapping_path = "8f749d51-c7c2-41cc-a55e-4fca8b97de73",
	 .access_mode = IBNBD_RO,
	 .sess = &g_s1,
	 .dev = &g_d[1]
	},
	{.mapping_path = "ecf6bfd0-3dae-46a3-9a1b-e61225920185",
	 .access_mode = IBNBD_MIGRATION,
	 .sess = &g_s1,
	 .dev = &g_d[2]
	},
	{.mapping_path = "ecf6bfd0-3dae-46a3-9a1b-e61225920185",
	 .access_mode = IBNBD_RW,
	 .sess = &g_s2,
	 .dev = &g_d[4]
	},
	{.mapping_path = "ecf6bfd0-3dae-46a3-9a1b-e61225920185",
	 .access_mode = IBNBD_RW,
	 .sess = &g_s2,
	 .dev = &g_d[3]
	},
	{.mapping_path = "ecf6bfd0-3dae-46a3-9a1b-e61225920185",
	 .access_mode = IBNBD_RW,
	 .sess = &g_s,
	 .dev = &g_d[3]
	},
};

static struct ibnbd_sess_dev *g_sds[] = {
	&g_sess_devs[0],
	&g_sess_devs[1],
	&g_sess_devs[2],
	&g_sess_devs[3],
	&g_sess_devs[4],
	&g_sess_devs[5],
	NULL
};
