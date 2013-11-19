#
# Makefile for trusty components
#

obj-$(CONFIG_TRUSTY)		+= trusty-core.o
trusty-core-objs			+= trusty.o trusty-mem.o
trusty-core-$(CONFIG_TRUSTY_SMC_ARM)	+= trusty-smc-arm.o
trusty-core-$(CONFIG_TRUSTY_SMC_ARM64)	+= trusty-smc-arm64.o
