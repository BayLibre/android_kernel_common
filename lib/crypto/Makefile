# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_CRYPTO_LIB_AES)			+= libaes.o
libaes-y					:= aes.o

obj-$(CONFIG_CRYPTO_LIB_ARC4)			+= libarc4.o
libarc4-y					:= arc4.o

obj-$(CONFIG_CRYPTO_LIB_BLAKE2S_GENERIC)	+= libblake2s-generic.o
libblake2s-generic-y				+= blake2s-generic.o

<<<<<<< HEAD   (4c5060 Merge 5.4.199 into android11-5.4-lts)
obj-$(CONFIG_CRYPTO_LIB_BLAKE2S)		+= libblake2s.o
libblake2s-y					+= blake2s.o

obj-$(CONFIG_CRYPTO_LIB_DES)			+= libdes.o
libdes-y					:= des.o

obj-$(CONFIG_CRYPTO_LIB_SHA256)			+= libsha256.o
libsha256-y					:= sha256.o

ifneq ($(CONFIG_CRYPTO_MANAGER_DISABLE_TESTS),y)
libblake2s-y					+= blake2s-selftest.o
=======
obj-$(CONFIG_CRYPTO_LIB_SHA256) += libsha256.o
libsha256-y := sha256.o

obj-y += libblake2s.o
libblake2s-y += blake2s.o blake2s-generic.o
ifneq ($(CONFIG_CRYPTO_MANAGER_DISABLE_TESTS),y)
libblake2s-y += blake2s-selftest.o
>>>>>>> BRANCH (f0c280 Linux 5.4.200)
endif
