# SPDX-License-Identifier: GPL-2.0
CFLAGS += -D_FILE_OFFSET_BITS=64 -Wall -Werror -I../.. -I../../../../..
LDLIBS := -lpthread
TEST_GEN_PROGS := fuse_test
TEST_GEN_FILES := test_trace.raw
EXTRA_CLEAN := *.bpf
BPF_FLAGS = -g \
	    -D__TARGET_ARCH_x86 \
	    -mlittle-endian \
	    -I ../../../../../include \
	    -idirafter /usr/lib/gcc/x86_64-linux-gnu/10/include \
	    -idirafter /usr/local/include \
	    -idirafter /usr/include/x86_64-linux-gnu \
	    -idirafter /usr/include \
	    -O2

include ../../lib.mk

# Put after include ../../lib.mk since that changes $(TEST_GEN_PROGS)
# Otherwise you get multiple targets, this becomes the default, and it's a mess
EXTRA_SOURCES :=
$(TEST_GEN_PROGS) : $(EXTRA_SOURCES)

%.raw: test_bpf.bpf
	bpf-objcopy -O binary --only-section=$(notdir $(basename $@)) $< $@

%.bpf: %.c
	bpf-gcc $(BPF_FLAGS) -c $< -o $@

