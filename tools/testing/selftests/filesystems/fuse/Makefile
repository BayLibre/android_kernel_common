# SPDX-License-Identifier: GPL-2.0
CFLAGS += -D_FILE_OFFSET_BITS=64 -Wall -Werror -I../.. -I../../../../..
LDLIBS := -lpthread
TEST_GEN_PROGS := fuse_test

include ../../lib.mk

# Put after include ../../lib.mk since that changes $(TEST_GEN_PROGS)
# Otherwise you get multiple targets, this becomes the default, and it's a mess
EXTRA_SOURCES :=
$(TEST_GEN_PROGS) : $(EXTRA_SOURCES)
