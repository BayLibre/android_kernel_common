/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Xiaomi Co., Ltd.
 *             http://www.xiaomi.com/
 */

#ifndef __F2FS_IOSTAT_H__
#define __F2FS_IOSTAT_H__

#ifdef CONFIG_XIAOMI_EROFS_IOSTAT
enum io_size_gran {
	SIZE_16K,
	SIZE_128K,
	SIZE_256K,
	SIZE_512K,
	SIZE_ABOVE,
	NR_IO_SIZE,
};

enum latency_ranges {
	RANGE_1MS = 0,
	RANGE_5MS,
	RANGE_10MS,
	RANGE_50MS,
	RANGE_ABOVE,
	NR_RANGES
};

#define TIME_WINDOWS 2
#define DEFAULT_WINDOW_PERIOD    60
#define THRES_MS_SIZE_16K   100
#define THRES_MS_SIZE_128K  100
#define THRES_MS_SIZE_256K  100
#define THRES_MS_SIZE_512K  100
#define THRES_MS_SIZE_ABOVE 100

#define DELAY_RANGE_1MS     (1 * NSEC_PER_MSEC)
#define DELAY_RANGE_5MS     (5 * NSEC_PER_MSEC)
#define DELAY_RANGE_10MS    (10 * NSEC_PER_MSEC)
#define DELAY_RANGE_50MS    (50 * NSEC_PER_MSEC)

struct latency_stats {
	unsigned long sum_lat;
	unsigned long peak_lat;
	unsigned int bio_cnt;
};

struct erofs_iostat_stats {
	unsigned int current_window_idx;
	unsigned long current_window_start;
	struct latency_stats window_stats[TIME_WINDOWS][NR_IO_SIZE];
	struct latency_stats window_ab_stats[TIME_WINDOWS][NR_IO_SIZE];
	struct latency_stats daily_stats[NR_IO_SIZE];
	struct latency_stats daily_ab_stats[NR_IO_SIZE];
	struct latency_stats delay_stats[NR_RANGES];
};

struct erofs_iostat {
	bool iostat_enable;
	unsigned long window_period_ns;
	unsigned long latency_threshold_ns[NR_IO_SIZE];
	struct erofs_iostat_stats __percpu *stats;
};

int erofs_iostat_config_parse(struct erofs_sb_info *sbi, const char *buf);
int erofs_iostat_daily_stats_reset(struct erofs_sb_info *sbi, const char *buf);
ssize_t erofs_iostat_daily_latency_show(struct erofs_sb_info *sbi, char *buf);
ssize_t erofs_iostat_window_latency_show(struct erofs_sb_info *sbi, char *buf);
void erofs_iostat_latency_stats_reset(struct erofs_sb_info *sbi);
void erofs_iostat_update(struct erofs_sb_info *sbi, struct bio *bio);
void erofs_iostat_record_start(struct erofs_sb_info *sbi, struct bio *bio);
int erofs_init_iostat(struct erofs_sb_info *sbi);
void erofs_destroy_iostat(struct erofs_sb_info *sbi);
#else
void erofs_iostat_update(struct erofs_sb_info *sbi, struct bio *bio) {}
void erofs_iostat_record_start(struct erofs_sb_info *sbi, struct bio *bio) {}
int erofs_init_iostat(struct erofs_sb_info *sbi) {return 0; }
void erofs_destroy_iostat(struct erofs_sb_info *sbi) {}
#endif
#endif

