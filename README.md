
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

Creating releases
=================

This project uses [semantic versioning](https://semver.org/). To create a
release, increase the version in `debian/changelog` and then commit the changes and tag the release:

```
gbp dch -R -a --git-author --ignore-branch
version=$(egrep -oe "[0-9,.]+" -m 1 debian/changelog)
git commit -asm "deb: Release rnbd $version"
git tag v$version
git push origin v$version
```

The xz-compressed release tarball can be generated by running:
```
name="rnbd-$version"
git archive --prefix="$name/" HEAD | xz -c9 > "../$name.tar.xz"
gpg --output "../$name.tar.xz.asc" --armor --detach-sign "../$name.tar.xz"
```

The package for can be built and uploaded by running:
```
TODO
```
