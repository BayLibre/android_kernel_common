// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/block_age.c
 *
 * Copyright (c) 2022 xiaomi Co., Ltd.
 *             http://www.xiaomi.com/
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>

#include "f2fs.h"
#include "segment.h"

static inline void f2fs_inc_data_block_alloc(struct f2fs_sb_info *sbi)
{
	atomic64_inc(&sbi->total_data_alloc);
}

void f2fs_init_block_age_info(struct f2fs_sb_info *sbi)
{
	atomic64_set(&sbi->total_data_alloc, 0);
}

void f2fs_inc_block_alloc_count(struct f2fs_sb_info *sbi, int type)
{
	if (IS_DATASEG(type))
		f2fs_inc_data_block_alloc(sbi);
}
