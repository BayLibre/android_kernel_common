// SPDX-License-Identifier: GPL-2.0
/*
 * f2fs iostat support
 *
 * Copyright 2021 Google LLC
 * Author: Daeho Jeong <daehojeong@google.com>
 */

#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/seq_file.h>

#include "f2fs.h"
#include "iostat.h"
#include <trace/events/f2fs.h>

static struct kmem_cache *bio_iostat_ctx_cache;
static mempool_t *bio_iostat_ctx_pool;

#ifdef CONFIG_XIAOMI_ENHANCED_IOSTAT
struct enhanced_iostat *iostat_array[MAX_F2FS_INSTANCES];

struct enhanced_iostat *f2fs_get_iostat(struct f2fs_sb_info *sbi)
{
	dev_t dev_id = sbi->sb->s_bdev->bd_dev;
	struct enhanced_iostat *iostat = NULL;
	int i;

	for (i = 0; i < MAX_F2FS_INSTANCES; i++) {
		if (iostat_array[i] && iostat_array[i]->dev_id == dev_id) {
			iostat = iostat_array[i];
			break;
		}
	}

	return iostat;
}

int iostat_array_size(void)
{
	int size = 0;
	int i;

	for (i = 0; i < MAX_F2FS_INSTANCES; i++) {
		if (iostat_array[i])
			size++;
	}

	return size;
}

static const char *io_type_to_str(enum io_type type)
{
	static const char * const type_str[] = {
		[IO_TYPE_READ]  = "READ",
		[IO_TYPE_WRITE] = "WRITE"
	};
	return type_str[type];
}

static const char *size_to_str(enum io_size_gran size)
{
	static const char * const size_str[] = {
		[SIZE_16K]   = "16",
		[SIZE_128K]  = "128",
		[SIZE_256K]  = "256",
		[SIZE_512K]  = "512",
		[SIZE_ABOVE] = "512+"
	};
	return size_str[size];
}

static const char *latency_range_to_str(enum latency_ranges range)
{
	static const char * const range_str[] = {
		[RANGE_1MS]    = "1",
		[RANGE_5MS]    = "5",
		[RANGE_10MS]   = "10",
		[RANGE_50MS]   = "50",
		[RANGE_ABOVE]  = "50+"
	};
	return range_str[range];
}

ssize_t iostat_daily_latency_show(struct f2fs_sb_info *sbi, char *buf)
{
	struct enhanced_iostat *iostat = f2fs_get_iostat(sbi);
	struct iostat_stats *stats;
	enum io_size_gran size;
	enum io_type type;
	ssize_t len = 0;
	unsigned long sum_lat, peak_lat;
	unsigned int bio_cnt;
	int cpu;

	if (!iostat || !iostat->en_iostat_enable)
		return sysfs_emit(buf, "I/O stats not available\n");

	len += sysfs_emit_at(buf, len, "Normal:\n%-10s %-16s %-11s %-16s\n",
			"Size(k)", "Sum(ms)", "Count", "Peak(ms)");

	for (type = 0; type < NUM_IO_TYPE; type++) {
		len += sysfs_emit_at(buf, len, "[%s]\n", io_type_to_str(type));
		for (size = 0; size < NR_IO_SIZE; size++) {
			sum_lat = 0;
			peak_lat = 0;
			bio_cnt = 0;
			for_each_possible_cpu(cpu) {
				stats = per_cpu_ptr(iostat->stats, cpu);
				sum_lat += stats->daily_stats[type][size].sum_lat;
				bio_cnt += stats->daily_stats[type][size].bio_cnt;
				peak_lat = max(stats->daily_stats[type][size].peak_lat, peak_lat);
			}
			len += sysfs_emit_at(buf, len,
				"%-10s %-16llu %-11u %-16llu\n",
				size_to_str(size),
				ktime_to_ms(sum_lat),
				bio_cnt,
				ktime_to_ms(peak_lat));
		}
	}

	len += sysfs_emit_at(buf, len, "\nAbnormal:\n%-10s %-16s %-11s %-16s %-16s\n",
			"Size(k)", "Sum(ms)", "Count", "Peak(ms)", "Threshold(ms)");

	for (type = 0; type < NUM_IO_TYPE; type++) {
		len += sysfs_emit_at(buf, len, "[%s]\n", io_type_to_str(type));
		for (size = 0; size < NR_IO_SIZE; size++) {
			sum_lat = 0;
			peak_lat = 0;
			bio_cnt = 0;
			for_each_possible_cpu(cpu) {
				stats = per_cpu_ptr(iostat->stats, cpu);
				sum_lat += stats->daily_ab_stats[type][size].sum_lat;
				bio_cnt += stats->daily_ab_stats[type][size].bio_cnt;
				peak_lat = max(stats->daily_ab_stats[type][size].peak_lat,
							peak_lat);
			}
			len += sysfs_emit_at(buf, len,
				"%-10s %-16llu %-11u %-16llu %-16lu\n",
				size_to_str(size),
				ktime_to_ms(sum_lat),
				bio_cnt,
				ktime_to_ms(peak_lat),
				iostat->latency_threshold_ns[size] / NSEC_PER_MSEC);
		}
	}

	len += sysfs_emit_at(buf, len, "\nDelay:\n%-10s %-16s %-11s %-16s\n",
			"Range(ms)", "Sum(ms)", "Count", "Peak(ms)");

	for (type = 0; type < NUM_IO_TYPE; type++) {
		len += sysfs_emit_at(buf, len, "[%s]\n", io_type_to_str(type));
		for (enum latency_ranges range = 0; range < NR_RANGES; range++) {
			sum_lat = 0;
			peak_lat = 0;
			bio_cnt = 0;
			for_each_possible_cpu(cpu) {
				stats = per_cpu_ptr(iostat->stats, cpu);
				sum_lat += stats->delay_stats[type][range].sum_lat;
				bio_cnt += stats->delay_stats[type][range].bio_cnt;
				peak_lat = max(stats->delay_stats[type][range].peak_lat, peak_lat);
			}

			len += sysfs_emit_at(buf, len,
				"%-10s %-16llu %-11u %-16llu\n",
				latency_range_to_str(range),
				ktime_to_ms(sum_lat),
				bio_cnt,
				ktime_to_ms(peak_lat));
		}
	}
	return len;
}

int iostat_daily_stats_reset(struct f2fs_sb_info *sbi, const char *buf, int count)
{
	struct enhanced_iostat *iostat = f2fs_get_iostat(sbi);
	struct iostat_stats *stats;
	const char *str = strim((char *)buf);
	int cpu;

	if (!iostat || !iostat->en_iostat_enable)
		return -ENODEV;

	if (strncmp(str, "reset", 5) != 0)
		return -EINVAL;

	for_each_possible_cpu(cpu) {
		stats = per_cpu_ptr(iostat->stats, cpu);
		memset(stats->daily_stats, 0, sizeof(stats->daily_stats));
		memset(stats->daily_ab_stats, 0, sizeof(stats->daily_ab_stats));
		memset(&stats->delay_stats, 0, sizeof(stats->delay_stats));
	}

	return count;
}

ssize_t iostat_window_latency_show(struct f2fs_sb_info *sbi, char *buf)
{
	struct enhanced_iostat *iostat = f2fs_get_iostat(sbi);
	struct iostat_stats *stats;
	struct latency_stats *lat, *ab_lat;
	enum io_type type;
	enum io_size_gran size;
	int window, idx;
	ssize_t len = 0;
	int cpu;
	unsigned long sum_lat, peak_lat;
	unsigned int bio_cnt;
	unsigned long period_ns;
	unsigned long now = ktime_get_ns();
	unsigned long elapsed_ns;

	if (!iostat || !iostat->en_iostat_enable)
		return sysfs_emit(buf, "I/O stats not available\n");

	period_ns = iostat->window_period_ns;

	len += sysfs_emit_at(buf, len, "Normal:\n%-8s %-12s %-16s %-16s %-16s\n",
			"Window", "Size(k)", "Sum(ms)", "Count", "Peak(ms)");

	for (type = 0; type < NUM_IO_TYPE; type++) {
		len += sysfs_emit_at(buf, len, "[%s]\n", io_type_to_str(type));
		for (window = 0; window < TIME_WINDOWS; window++) {
			for (size = 0; size < NR_IO_SIZE; size++) {
				sum_lat = 0;
				peak_lat = 0;
				bio_cnt = 0;
				for_each_possible_cpu(cpu) {
					stats = per_cpu_ptr(iostat->stats, cpu);
					elapsed_ns = now - stats->current_window_start;
					if (elapsed_ns > (window + 2) * period_ns)
						continue;
					idx = (stats->current_window_idx - window + TIME_WINDOWS)
						% TIME_WINDOWS;
					lat = &stats->window_stats[idx][type][size];
					sum_lat += lat->sum_lat;
					bio_cnt += lat->bio_cnt;
					peak_lat = max(lat->peak_lat, peak_lat);
				}

				if (bio_cnt > 0) {
					len += sysfs_emit_at(buf, len,
					"%-8d %-12s %-16llu %-16u %-16llu\n",
					idx,
					size_to_str(size),
					ktime_to_ms(sum_lat),
					bio_cnt,
					ktime_to_ms(peak_lat));
				}
			}
		}
	}

	len += sysfs_emit_at(buf, len, "\nAbnormal:\n%-8s %-12s %-16s %-16s %-16s %-16s\n",
			"Window", "Size(k)", "Sum(ms)", "Count", "Peak(ms)", "Threshold(ms)");
	for (type = 0; type < NUM_IO_TYPE; type++) {
		len += sysfs_emit_at(buf, len, "[%s]\n", io_type_to_str(type));
		for (window = 0; window < TIME_WINDOWS; window++) {
			for (size = 0; size < NR_IO_SIZE; size++) {
				sum_lat = 0;
				peak_lat = 0;
				bio_cnt = 0;
				for_each_possible_cpu(cpu) {
					stats = per_cpu_ptr(iostat->stats, cpu);
					elapsed_ns = now - stats->current_window_start;
					if (elapsed_ns > (window + 2) * period_ns)
						continue;
					idx = (stats->current_window_idx - window + TIME_WINDOWS)
						% TIME_WINDOWS;
					ab_lat = &stats->window_ab_stats[idx][type][size];
					sum_lat += ab_lat->sum_lat;
					bio_cnt += ab_lat->bio_cnt;
					peak_lat = max(ab_lat->peak_lat, peak_lat);
				}

				if (bio_cnt > 0) {
					len += sysfs_emit_at(buf, len,
					"%-8d %-12s %-16llu %-16u %-16llu %-16lu\n",
					idx,
					size_to_str(size),
					ktime_to_ms(sum_lat),
					bio_cnt,
					ktime_to_ms(peak_lat),
					iostat->latency_threshold_ns[size] / NSEC_PER_MSEC);
				}
			}
		}
	}

	return len;
}

void iostat_latency_stats_reset(struct f2fs_sb_info *sbi)
{
	struct enhanced_iostat *iostat = f2fs_get_iostat(sbi);
	struct iostat_stats *stats;
	int cpu;

	for_each_possible_cpu(cpu) {
		stats = per_cpu_ptr(iostat->stats, cpu);
		memset(&stats->window_stats, 0, sizeof(stats->window_stats));
		memset(&stats->window_ab_stats, 0, sizeof(stats->window_ab_stats));
		memset(&stats->daily_stats, 0, sizeof(stats->daily_stats));
		memset(&stats->daily_ab_stats, 0, sizeof(stats->daily_ab_stats));
		memset(&stats->delay_stats, 0, sizeof(stats->delay_stats));
	}
}

static inline enum io_size_gran get_io_size_granularity(unsigned int bio_size_bytes)
{
	unsigned int blks = bio_size_bytes >> 10;

	return (blks > 16) + (blks > 128) + (blks > 256) + (blks > 512);
}

static enum io_type get_io_type(struct bio *bio)
{
	return op_is_write(bio_op(bio)) ? IO_TYPE_WRITE : IO_TYPE_READ;
}

static inline enum latency_ranges get_latency_range(unsigned long latency)
{
	if (latency < DELAY_RANGE_1MS)
		return RANGE_1MS;
	else if (latency < DELAY_RANGE_5MS)
		return RANGE_5MS;
	else if (latency < DELAY_RANGE_10MS)
		return RANGE_10MS;
	else if (latency < DELAY_RANGE_50MS)
		return RANGE_50MS;
	else
		return RANGE_ABOVE;
}

static unsigned int update_time_windows(struct enhanced_iostat *iostat, unsigned long end_ts)
{
	struct iostat_stats *stats = this_cpu_ptr(iostat->stats);
	unsigned long elapsed_ns = end_ts - stats->current_window_start;
	unsigned long period_ns = iostat->window_period_ns;

	if (elapsed_ns > period_ns) {
		stats->current_window_start = end_ts;
		stats->current_window_idx = (stats->current_window_idx + 1) % TIME_WINDOWS;

		memset(&stats->window_stats[stats->current_window_idx], 0,
			  sizeof(stats->window_stats[stats->current_window_idx]));
		memset(&stats->window_ab_stats[stats->current_window_idx], 0,
			  sizeof(stats->window_ab_stats[stats->current_window_idx]));

	}

	return stats->current_window_idx;
}

static void __update_enhanced_iostat(struct bio_iostat_ctx *iostat_ctx, struct bio *bio)
{
	struct f2fs_sb_info *sbi = iostat_ctx->sbi;
	struct enhanced_iostat *iostat = f2fs_get_iostat(sbi);
	struct iostat_stats *stats;
	unsigned long latency, end_ts;
	enum io_type type = get_io_type(bio);
	enum io_size_gran gran;
	unsigned int window_idx;
	enum latency_ranges range;

	if (!iostat->en_iostat_enable)
		return;

	end_ts = ktime_get_ns();
	latency = end_ts - iostat_ctx->submit_ts;

	gran = get_io_size_granularity(bio->bi_iter.bi_size);
	range = get_latency_range(latency);

	preempt_disable();
	window_idx = update_time_windows(iostat, end_ts);

	stats = this_cpu_ptr(iostat->stats);
	stats->window_stats[window_idx][type][gran].sum_lat += latency;
	stats->window_stats[window_idx][type][gran].bio_cnt++;
	stats->window_stats[window_idx][type][gran].peak_lat =
		max(stats->window_stats[window_idx][type][gran].peak_lat, latency);

	stats->daily_stats[type][gran].sum_lat += latency;
	stats->daily_stats[type][gran].bio_cnt++;
	stats->daily_stats[type][gran].peak_lat =
		max(stats->daily_stats[type][gran].peak_lat, latency);

	if (latency > iostat->latency_threshold_ns[gran]) {
		stats->window_ab_stats[window_idx][type][gran].sum_lat += latency;
		stats->window_ab_stats[window_idx][type][gran].bio_cnt++;
		stats->window_ab_stats[window_idx][type][gran].peak_lat =
			stats->window_stats[window_idx][type][gran].peak_lat;
		stats->daily_ab_stats[type][gran].sum_lat += latency;
		stats->daily_ab_stats[type][gran].bio_cnt++;
		stats->daily_ab_stats[type][gran].peak_lat =
			stats->daily_stats[type][gran].peak_lat;
	}

	stats->delay_stats[type][range].sum_lat += latency;
	stats->delay_stats[type][range].bio_cnt++;
	stats->delay_stats[type][range].peak_lat =
		max(stats->delay_stats[type][range].peak_lat, latency);
	preempt_enable();
}
#else
static inline unsigned long long iostat_get_avg_bytes(struct f2fs_sb_info *sbi,
	enum iostat_type type)
{
	return sbi->iostat_count[type] ? div64_u64(sbi->iostat_bytes[type],
		sbi->iostat_count[type]) : 0;
}

#define IOSTAT_INFO_SHOW(name, type)					\
	seq_printf(seq, "%-23s %-16llu %-16llu %-16llu\n",		\
			name":", sbi->iostat_bytes[type],		\
			sbi->iostat_count[type],			\
			iostat_get_avg_bytes(sbi, type))

int __maybe_unused iostat_info_seq_show(struct seq_file *seq, void *offset)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	if (!sbi->iostat_enable)
		return 0;

	seq_printf(seq, "time:		%-16llu\n", ktime_get_real_seconds());
	seq_printf(seq, "\t\t\t%-16s %-16s %-16s\n",
				"io_bytes", "count", "avg_bytes");

	/* print app write IOs */
	seq_puts(seq, "[WRITE]\n");
	IOSTAT_INFO_SHOW("app buffered data", APP_BUFFERED_IO);
	IOSTAT_INFO_SHOW("app direct data", APP_DIRECT_IO);
	IOSTAT_INFO_SHOW("app mapped data", APP_MAPPED_IO);
	IOSTAT_INFO_SHOW("app buffered cdata", APP_BUFFERED_CDATA_IO);
	IOSTAT_INFO_SHOW("app mapped cdata", APP_MAPPED_CDATA_IO);

	/* print fs write IOs */
	IOSTAT_INFO_SHOW("fs data", FS_DATA_IO);
	IOSTAT_INFO_SHOW("fs cdata", FS_CDATA_IO);
	IOSTAT_INFO_SHOW("fs node", FS_NODE_IO);
	IOSTAT_INFO_SHOW("fs meta", FS_META_IO);
	IOSTAT_INFO_SHOW("fs gc data", FS_GC_DATA_IO);
	IOSTAT_INFO_SHOW("fs gc node", FS_GC_NODE_IO);
	IOSTAT_INFO_SHOW("fs cp data", FS_CP_DATA_IO);
	IOSTAT_INFO_SHOW("fs cp node", FS_CP_NODE_IO);
	IOSTAT_INFO_SHOW("fs cp meta", FS_CP_META_IO);

	/* print app read IOs */
	seq_puts(seq, "[READ]\n");
	IOSTAT_INFO_SHOW("app buffered data", APP_BUFFERED_READ_IO);
	IOSTAT_INFO_SHOW("app direct data", APP_DIRECT_READ_IO);
	IOSTAT_INFO_SHOW("app mapped data", APP_MAPPED_READ_IO);
	IOSTAT_INFO_SHOW("app buffered cdata", APP_BUFFERED_CDATA_READ_IO);
	IOSTAT_INFO_SHOW("app mapped cdata", APP_MAPPED_CDATA_READ_IO);

	/* print fs read IOs */
	IOSTAT_INFO_SHOW("fs data", FS_DATA_READ_IO);
	IOSTAT_INFO_SHOW("fs gc data", FS_GDATA_READ_IO);
	IOSTAT_INFO_SHOW("fs cdata", FS_CDATA_READ_IO);
	IOSTAT_INFO_SHOW("fs node", FS_NODE_READ_IO);
	IOSTAT_INFO_SHOW("fs meta", FS_META_READ_IO);

	/* print other IOs */
	seq_puts(seq, "[OTHER]\n");
	IOSTAT_INFO_SHOW("fs discard", FS_DISCARD_IO);
	IOSTAT_INFO_SHOW("fs flush", FS_FLUSH_IO);
	IOSTAT_INFO_SHOW("fs zone reset", FS_ZONE_RESET_IO);

	return 0;
}

static inline void __record_iostat_latency(struct f2fs_sb_info *sbi)
{
	int io, idx;
	struct f2fs_iostat_latency iostat_lat[MAX_IO_TYPE][NR_PAGE_TYPE];
	struct iostat_lat_info *io_lat = sbi->iostat_io_lat;
	unsigned long flags;

	spin_lock_irqsave(&sbi->iostat_lat_lock, flags);
	for (idx = 0; idx < MAX_IO_TYPE; idx++) {
		for (io = 0; io < NR_PAGE_TYPE; io++) {
			iostat_lat[idx][io].peak_lat =
			   jiffies_to_msecs(io_lat->peak_lat[idx][io]);
			iostat_lat[idx][io].cnt = io_lat->bio_cnt[idx][io];
			iostat_lat[idx][io].avg_lat = iostat_lat[idx][io].cnt ?
			   jiffies_to_msecs(io_lat->sum_lat[idx][io]) / iostat_lat[idx][io].cnt : 0;
			io_lat->sum_lat[idx][io] = 0;
			io_lat->peak_lat[idx][io] = 0;
			io_lat->bio_cnt[idx][io] = 0;
		}
	}
	spin_unlock_irqrestore(&sbi->iostat_lat_lock, flags);

	trace_f2fs_iostat_latency(sbi, iostat_lat);
}

static inline void f2fs_record_iostat(struct f2fs_sb_info *sbi)
{
	unsigned long long iostat_diff[NR_IO_TYPE];
	int i;
	unsigned long flags;

	if (time_is_after_jiffies(sbi->iostat_next_period))
		return;

	/* Need double check under the lock */
	spin_lock_irqsave(&sbi->iostat_lock, flags);
	if (time_is_after_jiffies(sbi->iostat_next_period)) {
		spin_unlock_irqrestore(&sbi->iostat_lock, flags);
		return;
	}
	sbi->iostat_next_period = jiffies +
				msecs_to_jiffies(sbi->iostat_period_ms);

	for (i = 0; i < NR_IO_TYPE; i++) {
		iostat_diff[i] = sbi->iostat_bytes[i] -
				sbi->prev_iostat_bytes[i];
		sbi->prev_iostat_bytes[i] = sbi->iostat_bytes[i];
	}
	spin_unlock_irqrestore(&sbi->iostat_lock, flags);

	trace_f2fs_iostat(sbi, iostat_diff);

	__record_iostat_latency(sbi);
}

void f2fs_reset_iostat(struct f2fs_sb_info *sbi)
{
	struct iostat_lat_info *io_lat = sbi->iostat_io_lat;
	int i;

	spin_lock_irq(&sbi->iostat_lock);
	for (i = 0; i < NR_IO_TYPE; i++) {
		sbi->iostat_count[i] = 0;
		sbi->iostat_bytes[i] = 0;
		sbi->prev_iostat_bytes[i] = 0;
	}
	spin_unlock_irq(&sbi->iostat_lock);

	spin_lock_irq(&sbi->iostat_lat_lock);
	memset(io_lat, 0, sizeof(struct iostat_lat_info));
	spin_unlock_irq(&sbi->iostat_lat_lock);
}

static inline void __f2fs_update_iostat(struct f2fs_sb_info *sbi,
			enum iostat_type type, unsigned long long io_bytes)
{
	sbi->iostat_bytes[type] += io_bytes;
	sbi->iostat_count[type]++;
}

void f2fs_update_iostat(struct f2fs_sb_info *sbi, struct inode *inode,
			enum iostat_type type, unsigned long long io_bytes)
{
	unsigned long flags;

	if (!sbi->iostat_enable)
		return;

	spin_lock_irqsave(&sbi->iostat_lock, flags);
	__f2fs_update_iostat(sbi, type, io_bytes);

	if (type == APP_BUFFERED_IO || type == APP_DIRECT_IO)
		__f2fs_update_iostat(sbi, APP_WRITE_IO, io_bytes);

	if (type == APP_BUFFERED_READ_IO || type == APP_DIRECT_READ_IO)
		__f2fs_update_iostat(sbi, APP_READ_IO, io_bytes);

#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (inode && f2fs_compressed_file(inode)) {
		if (type == APP_BUFFERED_IO)
			__f2fs_update_iostat(sbi, APP_BUFFERED_CDATA_IO, io_bytes);

		if (type == APP_BUFFERED_READ_IO)
			__f2fs_update_iostat(sbi, APP_BUFFERED_CDATA_READ_IO, io_bytes);

		if (type == APP_MAPPED_READ_IO)
			__f2fs_update_iostat(sbi, APP_MAPPED_CDATA_READ_IO, io_bytes);

		if (type == APP_MAPPED_IO)
			__f2fs_update_iostat(sbi, APP_MAPPED_CDATA_IO, io_bytes);

		if (type == FS_DATA_READ_IO)
			__f2fs_update_iostat(sbi, FS_CDATA_READ_IO, io_bytes);

		if (type == FS_DATA_IO)
			__f2fs_update_iostat(sbi, FS_CDATA_IO, io_bytes);
	}
#endif

	spin_unlock_irqrestore(&sbi->iostat_lock, flags);

	f2fs_record_iostat(sbi);
}

static inline void __update_iostat_latency(struct bio_iostat_ctx *iostat_ctx,
				enum iostat_lat_type lat_type)
{
	unsigned long ts_diff;
	unsigned int page_type = iostat_ctx->type;
	struct f2fs_sb_info *sbi = iostat_ctx->sbi;
	struct iostat_lat_info *io_lat = sbi->iostat_io_lat;
	unsigned long flags;

	if (!sbi->iostat_enable)
		return;

	ts_diff = jiffies - iostat_ctx->submit_ts;
	if (page_type == META_FLUSH) {
		page_type = META;
	} else if (page_type >= NR_PAGE_TYPE) {
		f2fs_warn(sbi, "%s: %d over NR_PAGE_TYPE", __func__, page_type);
		return;
	}

	spin_lock_irqsave(&sbi->iostat_lat_lock, flags);
	io_lat->sum_lat[lat_type][page_type] += ts_diff;
	io_lat->bio_cnt[lat_type][page_type]++;
	if (ts_diff > io_lat->peak_lat[lat_type][page_type])
		io_lat->peak_lat[lat_type][page_type] = ts_diff;
	spin_unlock_irqrestore(&sbi->iostat_lat_lock, flags);
}
#endif

void iostat_update_and_unbind_ctx(struct bio *bio)
{
	struct bio_iostat_ctx *iostat_ctx = bio->bi_private;
	enum iostat_lat_type lat_type;

	if (op_is_write(bio_op(bio))) {
		lat_type = bio->bi_opf & REQ_SYNC ?
				WRITE_SYNC_IO : WRITE_ASYNC_IO;
		bio->bi_private = iostat_ctx->sbi;
	} else {
		lat_type = READ_IO;
		bio->bi_private = iostat_ctx->post_read_ctx;
	}
#ifdef CONFIG_XIAOMI_ENHANCED_IOSTAT
	__update_enhanced_iostat(iostat_ctx, bio);
#else
	__update_iostat_latency(iostat_ctx, lat_type);
#endif
	mempool_free(iostat_ctx, bio_iostat_ctx_pool);
}

void iostat_alloc_and_bind_ctx(struct f2fs_sb_info *sbi,
		struct bio *bio, struct bio_post_read_ctx *ctx)
{
	struct bio_iostat_ctx *iostat_ctx;
	/* Due to the mempool, this never fails. */
	iostat_ctx = mempool_alloc(bio_iostat_ctx_pool, GFP_NOFS);
	iostat_ctx->sbi = sbi;
	iostat_ctx->submit_ts = 0;
	iostat_ctx->type = 0;
	iostat_ctx->post_read_ctx = ctx;
	bio->bi_private = iostat_ctx;
}

int __init f2fs_init_iostat_processing(void)
{
	bio_iostat_ctx_cache =
		kmem_cache_create("f2fs_bio_iostat_ctx",
				  sizeof(struct bio_iostat_ctx), 0, 0, NULL);
	if (!bio_iostat_ctx_cache)
		goto fail;
	bio_iostat_ctx_pool =
		mempool_create_slab_pool(NUM_PREALLOC_IOSTAT_CTXS,
					 bio_iostat_ctx_cache);
	if (!bio_iostat_ctx_pool)
		goto fail_free_cache;
	return 0;

fail_free_cache:
	kmem_cache_destroy(bio_iostat_ctx_cache);
fail:
	return -ENOMEM;
}

void f2fs_destroy_iostat_processing(void)
{
	mempool_destroy(bio_iostat_ctx_pool);
	kmem_cache_destroy(bio_iostat_ctx_cache);
}

int f2fs_init_iostat(struct f2fs_sb_info *sbi)
{
#ifdef CONFIG_XIAOMI_ENHANCED_IOSTAT
	dev_t dev_id = sbi->sb->s_bdev->bd_dev;
	struct enhanced_iostat *iostat;
	int i, cpu;

	if (f2fs_get_iostat(sbi))
		return 0;

	if (iostat_array_size() == MAX_F2FS_INSTANCES)
		return 0;

	iostat = f2fs_kzalloc(sbi, sizeof(struct enhanced_iostat), GFP_KERNEL);
	if (!iostat)
		return -ENOMEM;

	iostat->dev_id = dev_id;
	iostat->en_iostat_enable = false;
	iostat->window_period_ns = DEFAULT_WINDOW_PERIOD * NSEC_PER_SEC;
	iostat->latency_threshold_ns[SIZE_16K]   = THRES_MS_SIZE_16K   * NSEC_PER_MSEC;
	iostat->latency_threshold_ns[SIZE_128K]  = THRES_MS_SIZE_128K  * NSEC_PER_MSEC;
	iostat->latency_threshold_ns[SIZE_256K]  = THRES_MS_SIZE_256K  * NSEC_PER_MSEC;
	iostat->latency_threshold_ns[SIZE_512K]  = THRES_MS_SIZE_512K  * NSEC_PER_MSEC;
	iostat->latency_threshold_ns[SIZE_ABOVE] = THRES_MS_SIZE_ABOVE * NSEC_PER_MSEC;

	iostat->stats = alloc_percpu(struct iostat_stats);
	if (!iostat->stats) {
		kfree(iostat);
		return -ENOMEM;
	}
	for_each_possible_cpu(cpu) {
		struct iostat_stats *stats = per_cpu_ptr(iostat->stats, cpu);

		stats->current_window_idx = 0;
		stats->current_window_start = ktime_get_ns();

		memset(stats->window_stats, 0, sizeof(stats->window_stats));
		memset(stats->window_ab_stats, 0, sizeof(stats->window_ab_stats));
		memset(stats->daily_stats, 0, sizeof(stats->daily_stats));
		memset(stats->daily_ab_stats, 0, sizeof(stats->daily_ab_stats));
		memset(stats->delay_stats, 0, sizeof(stats->delay_stats));
	}

	for (i = 0; i < MAX_F2FS_INSTANCES; i++) {
		if (!iostat_array[i]) {
			iostat_array[i] = iostat;
			break;
		}
	}
#else
	/* init iostat info */
	spin_lock_init(&sbi->iostat_lock);
	spin_lock_init(&sbi->iostat_lat_lock);
	sbi->iostat_enable = false;
	sbi->iostat_period_ms = DEFAULT_IOSTAT_PERIOD_MS;
	sbi->iostat_io_lat = f2fs_kzalloc(sbi, sizeof(struct iostat_lat_info),
					GFP_KERNEL);
	if (!sbi->iostat_io_lat)
		return -ENOMEM;
#endif
	return 0;
}

void f2fs_destroy_iostat(struct f2fs_sb_info *sbi)
{
#ifdef CONFIG_XIAOMI_ENHANCED_IOSTAT
	dev_t dev_id = sbi->sb->s_bdev->bd_dev;
	int i;

	for (i = 0; i < MAX_F2FS_INSTANCES; i++) {
		if (iostat_array[i] && iostat_array[i]->dev_id == dev_id) {
			struct enhanced_iostat *iostat = iostat_array[i];

			if (iostat->stats)
				free_percpu(iostat->stats);
			kfree(iostat);
			iostat_array[i] = NULL;
			return;
		}
	}
#else
	kfree(sbi->iostat_io_lat);
#endif
}
