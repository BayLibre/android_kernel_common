# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2009 Simtec Electronics

<<<<<<< HEAD   (2a3f21 Merge 1cb86b6c3136 ("kbuild: save overridden KERNELRELEASE i)
include $(src)/Makefile.s3c64xx
=======
ifdef CONFIG_ARCH_S3C24XX
include $(srctree)/$(src)/Makefile.s3c24xx
endif

ifdef CONFIG_ARCH_S3C64XX
include $(srctree)/$(src)/Makefile.s3c64xx
endif
>>>>>>> BRANCH (534066 .gitignore: ignore *.cover and *.mbx)

# Objects we always build independent of SoC choice

obj-y				+= init.o cpu.o

# devices

obj-$(CONFIG_SAMSUNG_ATAGS)	+= platformdata.o
obj-$(CONFIG_SAMSUNG_ATAGS)	+= devs.o
obj-$(CONFIG_SAMSUNG_ATAGS)	+= dev-uart.o

obj-$(CONFIG_GPIO_SAMSUNG)     += gpio-samsung.o

# PM support

obj-$(CONFIG_SAMSUNG_PM)	+= pm.o pm-common.o
obj-$(CONFIG_SAMSUNG_PM_GPIO)	+= pm-gpio.o
obj-$(CONFIG_SAMSUNG_WAKEMASK)	+= wakeup-mask.o
