# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the FUSE filesystem.
#

obj-$(CONFIG_FUSE_FS) += fuse.o
obj-$(CONFIG_CUSE) += cuse.o
obj-$(CONFIG_VIRTIO_FS) += virtiofs.o

fuse-y := dev.o dir.o file.o inode.o control.o xattr.o acl.o readdir.o ioctl.o
<<<<<<< HEAD   (5ca75c ANDROID: gki_defconfig: Realign succinct defconfigs with lat)
fuse-y += passthrough.o
=======
fuse-y += iomode.o
>>>>>>> BRANCH (4cece7 Linux 6.9-rc1)
fuse-$(CONFIG_FUSE_DAX) += dax.o
fuse-$(CONFIG_FUSE_PASSTHROUGH) += passthrough.o

virtiofs-y := virtio_fs.o
