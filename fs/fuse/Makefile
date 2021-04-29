# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the FUSE filesystem.
#

obj-$(CONFIG_FUSE_FS) += fuse.o
obj-$(CONFIG_CUSE) += cuse.o
obj-$(CONFIG_VIRTIO_FS) += virtiofs.o

<<<<<<< HEAD   (9d00e6 ANDROID: Incremental fs: Fix pseudo-file attributes)
fuse-y := dev.o dir.o file.o inode.o control.o xattr.o acl.o readdir.o
fuse-y += passthrough.o
=======
fuse-y := dev.o dir.o file.o inode.o control.o xattr.o acl.o readdir.o ioctl.o
>>>>>>> BRANCH (e7c6e4 Fix misc new gcc warnings)
fuse-$(CONFIG_FUSE_DAX) += dax.o

virtiofs-y := virtio_fs.o
