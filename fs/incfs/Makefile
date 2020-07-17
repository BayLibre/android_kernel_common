# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_INCREMENTAL_FS)	:= incrementalfs.o

incrementalfs-y := \
	data_mgmt.o \
	format.o \
	integrity.o \
	main.o \
	vfs.o  \
	proc.o

incrementalfs-$(CONFIG_INCFS_STATS) += stats.o
