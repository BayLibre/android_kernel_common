/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google LLC
 * Author: Daeho Jeong <daehojeong@google.com>
 */
#ifndef __F2FS_IOSTAT_H__
#define __F2FS_IOSTAT_H__

struct bio_post_read_ctx;

enum iostat_lat_type {
	READ_IO = 0,
	WRITE_SYNC_IO,
	WRITE_ASYNC_IO,
	MAX_IO_TYPE,
};

#ifdef CONFIG_F2FS_IOSTAT

#define NUM_PREALLOC_IOSTAT_CTXS	128
#define DEFAULT_IOSTAT_PERIOD_MS	3000
#define MIN_IOSTAT_PERIOD_MS		100
/* maximum period of iostat tracing is 1 day */
#define MAX_IOSTAT_PERIOD_MS		8640000

struct iostat_lat_info {
	unsigned long sum_lat[MAX_IO_TYPE][NR_PAGE_TYPE];	/* sum of io latencies */
	unsigned long peak_lat[MAX_IO_TYPE][NR_PAGE_TYPE];	/* peak io latency */
	unsigned int bio_cnt[MAX_IO_TYPE][NR_PAGE_TYPE];	/* bio count */
};

#ifdef CONFIG_XIAOMI_ENHANCED_IOSTAT

#define MAX_F2FS_INSTANCES 5

enum io_type {
	IO_TYPE_READ,
	IO_TYPE_WRITE,
	NUM_IO_TYPE,
};

enum io_size_gran {
	SIZE_16K,
	SIZE_128K,
	SIZE_256K,
	SIZE_512K,
	SIZE_ABOVE,
	NR_IO_SIZE
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
#define DEFAULT_WINDOW_PERIOD 60
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

struct iostat_stats {
	unsigned int current_window_idx;
	unsigned long current_window_start;
	struct latency_stats window_stats[TIME_WINDOWS][NUM_IO_TYPE][NR_IO_SIZE];
	struct latency_stats window_ab_stats[TIME_WINDOWS][NUM_IO_TYPE][NR_IO_SIZE];
	struct latency_stats daily_stats[NUM_IO_TYPE][NR_IO_SIZE];
	struct latency_stats daily_ab_stats[NUM_IO_TYPE][NR_IO_SIZE];
	struct latency_stats delay_stats[NUM_IO_TYPE][NR_RANGES];
};

struct enhanced_iostat {
	dev_t dev_id;
	bool en_iostat_enable;
	unsigned long window_period_ns;
	unsigned long latency_threshold_ns[NR_IO_SIZE];
	struct iostat_stats __percpu *stats;
};

extern struct enhanced_iostat *iostat_array[MAX_F2FS_INSTANCES];

struct enhanced_iostat *f2fs_get_iostat(struct f2fs_sb_info *sbi);
int iostat_array_size(void);
int iostat_daily_stats_reset(struct f2fs_sb_info *sbi, const char *buf, int count);
ssize_t iostat_daily_latency_show(struct f2fs_sb_info *sbi, char *buf);
ssize_t iostat_window_latency_show(struct f2fs_sb_info *sbi, char *buf);
void iostat_latency_stats_reset(struct f2fs_sb_info *sbi);
static inline void f2fs_update_iostat(struct f2fs_sb_info *sbi, struct inode *inode,
		enum iostat_type type, unsigned long long io_bytes) {}
#else
extern int __maybe_unused iostat_info_seq_show(struct seq_file *seq,
			void *offset);
extern void f2fs_reset_iostat(struct f2fs_sb_info *sbi);
extern void f2fs_update_iostat(struct f2fs_sb_info *sbi, struct inode *inode,
			enum iostat_type type, unsigned long long io_bytes);
#endif

struct bio_iostat_ctx {
	struct f2fs_sb_info *sbi;
	unsigned long submit_ts;
	enum page_type type;
	struct bio_post_read_ctx *post_read_ctx;
};

static inline void iostat_update_submit_ctx(struct bio *bio,
			enum page_type type)
{
	struct bio_iostat_ctx *iostat_ctx = bio->bi_private;

#ifdef CONFIG_XIAOMI_ENHANCED_IOSTAT
	iostat_ctx->submit_ts = ktime_get_ns();
#else
	iostat_ctx->submit_ts = jiffies;
#endif
	iostat_ctx->type = type;
}

static inline struct bio_post_read_ctx *get_post_read_ctx(struct bio *bio)
{
	struct bio_iostat_ctx *iostat_ctx = bio->bi_private;

	return iostat_ctx->post_read_ctx;
}

extern void iostat_update_and_unbind_ctx(struct bio *bio);
extern void iostat_alloc_and_bind_ctx(struct f2fs_sb_info *sbi,
		struct bio *bio, struct bio_post_read_ctx *ctx);
extern int f2fs_init_iostat_processing(void);
extern void f2fs_destroy_iostat_processing(void);
extern int f2fs_init_iostat(struct f2fs_sb_info *sbi);
extern void f2fs_destroy_iostat(struct f2fs_sb_info *sbi);
#else
static inline void f2fs_update_iostat(struct f2fs_sb_info *sbi, struct inode *inode,
		enum iostat_type type, unsigned long long io_bytes) {}
static inline void iostat_update_and_unbind_ctx(struct bio *bio) {}
static inline void iostat_alloc_and_bind_ctx(struct f2fs_sb_info *sbi,
		struct bio *bio, struct bio_post_read_ctx *ctx) {}
static inline void iostat_update_submit_ctx(struct bio *bio,
		enum page_type type) {}
static inline struct bio_post_read_ctx *get_post_read_ctx(struct bio *bio)
{
	return bio->bi_private;
}
static inline int f2fs_init_iostat_processing(void) { return 0; }
static inline void f2fs_destroy_iostat_processing(void) {}
static inline int f2fs_init_iostat(struct f2fs_sb_info *sbi) { return 0; }
static inline void f2fs_destroy_iostat(struct f2fs_sb_info *sbi) {}
#endif
#endif /* __F2FS_IOSTAT_H__ */
