/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */
#ifndef _BUILTIN_H
#define _BUILTIN_H

#include <subcmd/parse-options.h>

extern const struct option check_options[];
<<<<<<< HEAD   (3f05c6 ANDROID: fix up 5.10.132 merge with the virtio_mmio.c driver)
extern bool no_fp, no_unreachable, retpoline, module, backtrace, uaccess, stats, validate_dup, vmlinux, mcount, noinstr;
=======
extern bool no_fp, no_unreachable, retpoline, module, backtrace, uaccess, stats,
            validate_dup, vmlinux, sls, unret, rethunk;
>>>>>>> BRANCH (503493 Linux 5.10.133)

extern int cmd_check(int argc, const char **argv);
extern int cmd_orc(int argc, const char **argv);

#endif /* _BUILTIN_H */
