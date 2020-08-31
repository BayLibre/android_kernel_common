#
# Makefile for trusty components
#

obj-$(CONFIG_TRUSTY_ARM)	+= trusty-core-arm.o
trusty-core-arm-objs += trusty.o trusty-mem.o trusty-smc-arm.o
obj-$(CONFIG_TRUSTY_ARM64)	+= trusty-core-arm64.o
trusty-core-arm64-objs += trusty.o trusty-mem.o trusty-smc-arm64.o
obj-$(CONFIG_TRUSTY_IRQ)	+= trusty-irq.o
obj-$(CONFIG_TRUSTY_LOG)	+= trusty-log.o
obj-$(CONFIG_TRUSTY_TEST)	+= trusty-test.o
obj-$(CONFIG_TRUSTY_VIRTIO)	+= trusty-virtio.o
obj-$(CONFIG_TRUSTY_VIRTIO_IPC)	+= trusty-ipc.o
