/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _ANDROID_VENDOR_DEBUG_PARSER_H
#define _ANDROID_VENDOR_DEBUG_PARSER_H

#include <linux/seq_buf.h>

#ifdef CONFIG_ANDROID_VENDOR_DEBUG_PARSER

void android_dump_runqueues(struct seq_buf *runq_buf);

#else /* !CONFIG_ANDROID_VENDOR_DEBUG_PARSER */

static void android_dump_runqueues(struct seq_buf *runq_buf)
{
}

#endif /* CONFIG_ANDROID_VENDOR_DEBUG_PARSER */


#endif /* _ANDROID_VENDOR_DEBUG_PARSER_H */
