KASAN_SANITIZE := n

obj-y = bootmem.o \
	vmalloc.o \
	kasan_init.o

ifeq ($(CONFIG_MMU),y)
obj-y += tlb.o
endif
