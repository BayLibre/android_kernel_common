# SPDX-License-Identifier: GPL-2.0
ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE=VFS_INTERNAL_I_AM_REALLY_A_FILESYSTEM_AND_AM_NOT_A_DRIVER

obj-$(CONFIG_INCREMENTAL_FS)	+= incrementalfs.o

incrementalfs-y := \
	data_mgmt.o \
	format.o \
	integrity.o \
	main.o \
	vfs.o
