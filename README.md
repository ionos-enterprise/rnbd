
rnbd
====

[RNBD](https://github.com/torvalds/linux/tree/master/drivers/block/rnbd)
 (RDMA Network Block Device) and
 [RTRS](https://github.com/torvalds/linux/tree/master/drivers/infiniband/ulp/rtrs) (RDMA Transport Library)
are two pairs of kernel modules (client and server) that allow for
remote access to a block device over RDMA (InfiniBand, RoCE, iWARP).

**rndb** is a tool which allows user to configure said kernel modules
in a convienient way.

For the description of the interface see [Manpage](https://github.com/ionos-enterprise/rnbd/blob/master/rnbd.8.md).
